// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2024, Nuclei - All Rights Reserved
 *
 * HSM Mailbox 接口定义
 *
 * 本文件定义了 TEE 与 HSM（硬件安全模块）通信的 Mailbox 接口。
 * Mailbox 是一种基于共享内存的同步通信机制，TEE 通过 Mailbox 向 HSM
 * 发送加密/哈希/随机数等安全服务请求，并接收处理结果。
 *
 * 使用流程：
 * 1. 调用 mailbox_avaliable_linked_num() 获取可用邮箱号
 * 2. 准备命令数据（根据操作类型选择对应的结构体）
 * 3. 发送前执行 cache_operation(TEE_CACHEFLUSH, ...) 同步数据到物理内存
 * 4. 调用 mailbox_secure_service_host_send() 发送命令
 * 5. 调用 mailbox_secure_service_host_receive() 接收响应
 * 6. 执行 cache_operation(TEE_CACHEINVALIDATE, ...) 确保读到 HSM 写入的数据
 * 7. 检查响应数据的错误标志位
 *
 * 支持的 Opcode（操作类型，定义于 nuclei_hsm_abi.h）：
 * - SECURE_SERVICE_OPCODE_HASH (1): 哈希运算（MD5/SHA/SM3）
 * - SECURE_SERVICE_OPCODE_CRYP (2): 对称加密（AES/SM4）
 * - SECURE_SERVICE_OPCODE_ACRYP (3): 非对称加密（RSA）
 * - SECURE_SERVICE_OPCODE_TRNG (4): 真随机数生成
 */
#ifndef __NUCLEI_HSM_MAILBOX_H__
#define __NUCLEI_HSM_MAILBOX_H__

#include <string.h>
#include <stdint.h>

/**
 * @brief 获取可用的 Mailbox 邮箱编号
 *
 * 在使用 Mailbox 发送命令前，需要先获取一个可用的邮箱。
 * 当前系统配置支持 MAILBOX_AVALIABLE_MAX_NUM (1) 个邮箱。
 *
 * @return int8_t 邮箱编号（>=0）表示成功，-1 表示无可用邮箱
 *
 * @note 建议配合超时重试机制使用：
 * @code
 *   uint32_t timeout = 100;
 *   int8_t mbox_num;
 *   do {
 *       mbox_num = mailbox_avaliable_linked_num();
 *       if (mbox_num != -1) break;
 *   } while (--timeout != 0);
 *   if (mbox_num == -1) return TEE_ERROR_BUSY;
 * @endcode
 */
int8_t mailbox_avaliable_linked_num(void);

/**
 * @brief 通过 Mailbox 向 HSM 发送安全服务命令
 *
 * 该函数会：
 * 1. 链接到指定的 Mailbox
 * 2. 根据 opcode 将命令数据复制到内部缓冲区
 * 3. 将数据写入 Mailbox 输入寄存器
 * 4. 设置输入满标志，通知 HSM 处理
 *
 * @param data 命令数据指针，根据 opcode 选择对应结构体：
 *             - SECURE_SERVICE_OPCODE_HASH: mailbox_hash_cmd_in_token
 *             - SECURE_SERVICE_OPCODE_CRYP: mailbox_cryp_cmd_in_token
 *             - SECURE_SERVICE_OPCODE_ACRYP: acryp_in_token_t
 * @param opcode 操作类型（SECURE_SERVICE_OPCODE_*）
 * @param mailbox_num 邮箱编号（通过 mailbox_avaliable_linked_num 获取）
 *
 * @warning 调用前必须确保数据已写入物理内存（执行 cache flush）
 * @warning 命令数据中的地址必须使用 virt_to_phys() 转换为物理地址
 *
 * @see SECURE_SERVICE_OPCODE_HASH
 * @see SECURE_SERVICE_OPCODE_CRYP
 * @see SECURE_SERVICE_OPCODE_ACRYP
 */
void mailbox_secure_service_host_send(uint32_t *data, uint8_t opcode, uint8_t mailbox_num);

/**
 * @brief 从 Mailbox 接收 HSM 的响应数据
 *
 * 该函数会：
 * 1. 轮询等待输出满标志（HSM 处理完成）
 * 2. 从 Mailbox 输出寄存器读取数据
 * 3. 清除输出满标志
 * 4. 断开 Mailbox 链接
 *
 * @param data 接收缓冲区指针，大小至少 32 个 uint32_t（128 字节）
 * @param mailbox_num 邮箱编号（与发送时使用的相同）
 *
 * @note 响应数据格式：
 *       - rbuf[0]: 状态/错误码（bit 31 为错误标志，bits [28:24] 为错误码）
 *       - rbuf[1]: 保留
 *       - rbuf[2+]: 实际返回数据（哈希结果、加密结果等）
 *
 * @warning 接收后必须执行 cache invalidate 确保读取到 HSM 写入的最新数据
 * @warning 检查 rbuf[0] 的 bit 31，如果置位表示 HSM 返回错误
 *
 * 错误检查示例：
 * @code
 *   uint32_t rbuf[32];
 *   mailbox_secure_service_host_receive(rbuf, mailbox_num);
 *   if (rbuf[0] & BIT(31)) {
 *       uint8_t err_code = (rbuf[0] >> 24) & 0x1F;
 *       EMSG("HSM error: %x", err_code);
 *       return TEE_ERROR_GENERIC;
 *   }
 * @endcode
 */
void mailbox_secure_service_host_receive(uint32_t *data, int8_t mailbox_num);

#endif