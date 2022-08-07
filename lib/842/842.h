/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __842_H__
#define __842_H__

/**
 * @brief
 *  842压缩格式由多个块组成。每个块都是如下格式：
 *      <template> [arg1] [arg2] [arg3] [arg4]
 *  其中存在 0 - 4 个模板参数，具体取决于特定的模板操作。对于正常操作，
 *  每个参数要么是添加到输出缓冲区的特定数据字节数，要么是指向要复制到输出缓冲区里
 *  的先前写入的数据字节数的索引。
 *
 *  模板码的值是 5bit。这个值指示如何处理如下数据：
 *      1. 0 - 0x19 应该使用模板表，即解压缩中使用的静态的 "decomp_ops"，对于每个模板(表行)，
 *  有 1 到 4 个操作；每个动作对应于模板代码位后面的一个参数。每个动作要么是"data"型操作，要么
 *  是 "索引" 型操作。每个动作结果都是2、4 或8个字节，并把他们写入到缓存区。每个模板(即表行中的所有操作)
 *  将加起来最多 8 个字节写入输出缓存区。任何少于 4 个操作的行都会填充 "noop" 操作，由 N0 表示(压缩
 *  数据缓冲区中没有相应的arg)
 *
 *  表中由 D2、D4、D8表示的 "Data" 操作意味着压缩数据缓冲区中相应的 arg 分别是: 2字节、4字节或8字节，
 *  应该直接复制到输出缓冲区。
 *
 *  表中由 I2、I4、I8表示的 "Index" 操作意味着相应的 arg 是一个索引参数，分别指向输出缓冲区中已经存在的
 *  2、4或8字节值，应该复制到输出缓冲区的末尾。本质上，索引指向环形缓冲区中的一个位置，该位置包含输出缓冲区
 *  数据的最后N个字节。每个索引的arg位数位：I2为bits、I4为9bits、I8为8bits。由于每个索引指向一个2、4或8
 *  字节的部分，这意味着 I2 可以引用 512字节((2 ^ 8bits = 256) * 2 bits)，I4可以引用2048字节((2 ^ 9 = 512) * 4字节)，
 *  I8 可以引用 2048 bytes ((2 ^ 8 = 256) * 8bits)。可以将其视为 I2、I4和I8中每一个的一种环形缓冲区，
 *  它们针对写入输出缓冲区的每个字节进行更新。在这个实现中，输出缓冲区直接用于每个索引；不需要额外的内存。请注意，
 *  索引是进入环形缓冲区，而不是滑动窗口；例如，如果260个字节写入输出缓冲区，那么I2[0]将索引到输出缓冲区中的256字节，
 *  而I2[16]则索引到输出缓冲区中的16字节。
 *
 *  还有3个特殊的模板代码；0X1B 表示"重复"; 0X1C 表示"零"; 0X1E表示"结束"。
 *  “重复”操作之后是一个6位 arg N，表示要重复多少次。写入输出缓冲器的最后 8 个字节再次写入输出缓冲器，N + 1次。
 *  "零"操作没有 arg 位，将 8 个零写入输出缓冲区。
 *  "结束"操作也没有arg位，表示压缩数据结束。
 *  在结束操作位之后可能会有一些填充(通常是0)位，以将缓冲区长度填充到特定的字节倍数(通常位 8、16或32字节的倍数)。
 *
 *  次软件的实现还使用一个未定义的模板值 0X1D 作为特殊的"短数据"模板代码，以表示少于8字节的未压缩数据。
 *  它后面是一个 3 位的 arg N，指示后面将有多少数据字节，然后是 N 字节的数据，这些数据应复制到输出缓冲区。
 *  这允许软件在 842 压缩器接收不是 8 字节长的精确倍数的输入缓冲器。但是， 842 硬件解压缩器将拒绝包含此纯软件模板
 *  的压缩缓冲区，并且必须使用此软件库进行解压缩。842软件压缩模块包含一个参数，用于禁用此仅限软件"短数据"模板，而只需拒绝任何
 *  长度不是 8 字节倍数的输入缓冲区。
 *
 *  在处理每个操作代码的所有操作后，另一个模板代码位于接下来的5位。一旦监测到"结束"模板代码，解压缩就结束。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/crc32.h>
#include <asm/unaligned.h>

#include <linux/sw842.h>

/* special templates */
#define OP_REPEAT	(0x1B)
#define OP_ZEROS	(0x1C)
#define OP_END		(0x1E)

/* sw only template - this is not in the hw design; it's used only by this
 * software compressor and decompressor, to allow input buffers that aren't
 * a multiple of 8.
 */
#define OP_SHORT_DATA	(0x1D)

/* additional bits of each op param */
#define OP_BITS		(5)
#define REPEAT_BITS	(6)
#define SHORT_DATA_BITS	(3)
#define I2_BITS		(8)
#define I4_BITS		(9)
#define I8_BITS		(8)
#define CRC_BITS	(32)

#define REPEAT_BITS_MAX		(0x3f)
#define SHORT_DATA_BITS_MAX	(0x7)

/* Arbitrary values used to indicate action */
#define OP_ACTION	(0x70)
#define OP_ACTION_INDEX	(0x10)
#define OP_ACTION_DATA	(0x20)
#define OP_ACTION_NOOP	(0x40)
#define OP_AMOUNT	(0x0f)
#define OP_AMOUNT_0	(0x00)
#define OP_AMOUNT_2	(0x02)
#define OP_AMOUNT_4	(0x04)
#define OP_AMOUNT_8	(0x08)

#define D2		(OP_ACTION_DATA  | OP_AMOUNT_2)
#define D4		(OP_ACTION_DATA  | OP_AMOUNT_4)
#define D8		(OP_ACTION_DATA  | OP_AMOUNT_8)
#define I2		(OP_ACTION_INDEX | OP_AMOUNT_2)
#define I4		(OP_ACTION_INDEX | OP_AMOUNT_4)
#define I8		(OP_ACTION_INDEX | OP_AMOUNT_8)
#define N0		(OP_ACTION_NOOP  | OP_AMOUNT_0)

/* the max of the regular templates - not including the special templates */
#define OPS_MAX		(0x1a)

#endif
