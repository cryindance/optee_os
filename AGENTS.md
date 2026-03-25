# OP-TEE OS 核心知识库

**组件**: TEE安全操作系统核心
**规模**: ~1800文件，核心代码
**架构**: RISC-V S-mode运行
**主要语言**: C + RISC-V汇编

---

## 目录结构

```
optee_os/
├── core/
│   ├── arch/riscv/        # RISC-V架构代码
│   │   ├── plat-nuclei/   # 芯来平台实现
│   │   ├── kernel/        # 线程、中断、启动
│   │   ├── mm/            # 内存管理(MMU/PMP)
│   │   └── tee/           # 系统调用处理
│   ├── kernel/            # 核心调度
│   ├── mm/                # 内存管理
│   ├── drivers/           # 设备驱动
│   ├── crypto/            # 加密服务
│   ├── tee/               # TEE服务
│   └── pta/               # 伪可信应用
├── lib/                   # 库(libutee, mbedtls等)
├── ta/                    # 内置TA架构支持
└── mk/                    # Makefile框架
```

---

## 关键定位

| 任务 | 文件/目录 |
|------|-----------|
| **添加平台支持** | `core/arch/riscv/plat-<platform>/` |
| **修改中断处理** | `core/arch/riscv/plat-nuclei/main.c` |
| **添加系统调用** | `core/tee/tee_svc_*.c` |
| **修改内存布局** | `core/arch/riscv/plat-nuclei/conf.mk` |
| **添加驱动** | `core/drivers/<driver>/` + `sub.mk` |
| **调试日志** | 使用 `DMSG()`/`IMSG()`/`EMSG()` |

---

## 核心API

### 线程管理
- `thread_get_exceptions()` - 获取异常状态
- `thread_set_exceptions()` - 设置异常屏蔽
- `thread_std_smc_entry()` - SMC入口处理

### 内存管理
- `core_mmu_get_va()` - 物理地址转虚拟地址
- `register_phys_mem()` - 注册物理内存区域
- `phys_to_virt()` / `virt_to_phys()` - 地址转换

### 中断处理
- `itr_core_handler()` - 核心中断处理(PLIC)
- `sbi_register_secure_intr()` - 注册安全中断

---

## 平台移植

### 必需文件
```
plat-<name>/
├── conf.mk              # 编译配置
├── platform_config.h    # 外设地址定义
├── main.c               # 平台初始化
└── sub.mk               # 构建规则
```

### 关键配置项
```makefile
CFG_TZDRAM_START ?= 0x41800000    # TEE内存起始
CFG_TZDRAM_SIZE  ?= 0x00800000    # TEE内存大小
CFG_SHMEM_START  ?= 0x41200000    # 共享内存起始
CFG_NUM_THREADS=4                  # 线程数
CFG_TEE_CORE_NB_CORE=8             # CPU核心数
```

---

## 构建命令

```bash
# 基本构建
make -C optee_os O=out

# 调试构建
make O=out CFG_TEE_CORE_LOG_LEVEL=4 CFG_TEE_CORE_DEBUG=y

# 生产构建
make O=out CFG_TEE_CORE_LOG_LEVEL=0 CFG_TEE_CORE_DEBUG=n

# 清理
make clean
```

---

## 加密子系统 (Crypto)

### 架构层次
```
TEE Crypto API (libutee)
    ↓
drvcrypt 抽象层 (core/drivers/crypto/crypto_api/)
    ↓
硬件驱动层 (nuclei/caam/se050/stm32)
    ↓
硬件加速引擎 (HSM/CAAM/SE050等)
```

### 目录结构
```
core/drivers/crypto/
├── crypto_api/              # 驱动抽象层
│   ├── drvcrypt.c           # 驱动注册管理器
│   ├── include/drvcrypt*.h  # 各类算法接口定义
│   ├── cipher/              # 对称加密通用代码
│   ├── hash/                # 哈希通用代码
│   └── ...
├── nuclei/                  # 芯来HSM硬件加速
│   ├── nuclei_hsm_cryp.c    # 初始化入口 (early_init)
│   ├── cipher.c             # AES/SM4硬件实现
│   ├── hash.c               # MD5/SHA/SM3硬件实现
│   ├── hmac.c               # HMAC实现
│   ├── rsa.c                # RSA实现
│   ├── mailbox.c            # HSM通信邮箱驱动
│   └── nuclei_hsm_abi.h     # HSM指令格式定义
├── caam/                    # NXP CAAM驱动
├── se050/                   # NXP SE050安全元件
└── stm32/                   # STM32加密硬件
```

### 关键API

**驱动注册** (`drvcrypt.h`)
```c
drvcrypt_register(CRYPTO_CIPHER, &ops);   // 注册对称加密
drvcrypt_register(CRYPTO_HASH, &ops);     // 注册哈希
drvcrypt_register(CRYPTO_RSA, &ops);      // 注册RSA
drvcrypt_get_ops(CRYPTO_CIPHER);          // 获取已注册驱动
```

**对称加密接口** (`drvcrypt_cipher.h`)
```c
struct drvcrypt_cipher {
    TEE_Result (*alloc_ctx)(void **ctx, uint32_t algo);
    TEE_Result (*init)(struct drvcrypt_cipher_init *dinit);
    TEE_Result (*update)(struct drvcrypt_cipher_update *dupdate);
    void (*final)(void *ctx);
    void (*free_ctx)(void *ctx);
};
```

### 芯来HSM实现要点

**1. 初始化流程** (`nuclei_hsm_cryp.c`)
```c
early_init(nuclei_hsm_cryp_init)  // 系统启动时自动调用
├── nuclei_register_cipher()     // 注册AES/SM4
├── nuclei_register_hash()       // 注册MD5/SHA/SM3
├── nuclei_register_hmac()       // 注册HMAC
├── nuclei_register_rsa()        // 注册RSA
└── core_mmu_get_va()            // 映射Mailbox内存
```

**2. Mailbox通信** (`mailbox.c`)
```c
// 命令发送
mailbox_secure_service_host_send(data, opcode, mbox_num);
// opcode: SECURE_SERVICE_OPCODE_HASH/CRYPT/ACRYP/TRNG

// 结果接收
mailbox_secure_service_host_receive(rbuf, mbox_num);

// 可用邮箱查询
mailbox_avaliable_linked_num();
```

**3. 命令结构体差异**

虽然 Mailbox 通信调用模式统一，但不同密码学接口在命令结构体填充上有明显差异：

| 维度 | 对称加密 (Cipher) | 哈希 (Hash) | HMAC | RSA |
|------|------------------|-------------|------|-----|
| **命令结构体** | `mailbox_cryp_cmd_in_token` | `mailbox_hash_cmd_in_token` | 复用 hash | `acryp_in_token_t` |
| **Opcode** | `SECURE_SERVICE_OPCODE_CRYP` | `SECURE_SERVICE_OPCODE_HASH` | 复用 HASH | `SECURE_SERVICE_OPCODE_ACRYP` |
| **关键字段** | IV[4], key[8] | key[32]（保留） | key[32]（预计算密钥） | signdata, key 地址 |
| **cmd_cfg.algo** | `CRYP_AES` / `CRYP_SM4` | `HASH_SHA256` 等 | 复用 hash 算法 | `ACRYP_MOD_EXP` |
| **cmd_cfg.mode** | `ECB/CBC/CTR` | `HASH_MODE` | `HMAC_MODE` | `MOD_EXP_SPECIFY` |
| **特殊处理** | 密钥长度选择 | in_ctrl 控制分块 | 密钥预哈希 | 地址以 word 计 |

**共同模式**：
1. 填充 `header.opcode` - 区分服务类型
2. 设置物理地址 - 使用 `virt_to_phys()` 转换
3. 配置 `cmd_cfg` - algo + mode + 控制位
4. Cache 同步 - flush 发送数据，invalidate 接收结果
5. 错误检查 - `rbuf[0] & BIT(31)` 检查 HSM 错误

**4. 算法支持**

| 类型 | 算法 | 模式 |
|------|------|------|
| 对称加密 | AES-128/192/256 | ECB, CBC, CTR |
| 对称加密 | SM4 | ECB, CBC, CTR |
| 哈希 | MD5, SHA1/224/256/384/512, SM3 | - |
| HMAC | HMAC-SHA256等 | - |
| 非对称 | RSA-2048/4096 | 签名/验签 |

**5. HSM指令格式** (`nuclei_hsm_abi.h`)
```c
typedef struct {
    struct common_head_t header;   // opcode: HASH/CRYPT/ACRYP/TRNG
    uint32_t input_data_addr_low;  // 输入数据地址(低32位)
    uint32_t input_data_addr_hig;  // 输入数据地址(高32位)
    struct cmd_cfg_t cmd_cfg;      // 算法配置
    // ...
} mailbox_hash_cmd_in_token;
```

### 配置启用

```makefile
# 启用芯来HSM加密
CFG_NUCLEI_HSM_CRYPTO=y

# 启用驱动框架(自动设置)
CFG_CRYPTO_DRIVER=y
CFG_CRYPTO_DRIVER_DEBUG=0  # 调试日志级别
```

### 关键定位

| 任务 | 文件 |
|------|------|
| 添加新算法 | `core/drivers/crypto/nuclei/` 下新增文件 |
| 修改HSM通信 | `mailbox.c` |
| 调整算法参数 | `nuclei_hsm_abi.h` |
| 调试加密问题 | 开启 `CFG_CRYPTO_DRIVER_DEBUG=3` |

---

## 添加硬件加密驱动（以芯来HSM为例）

参考提交: `518f48089 driver: add Nuclei HSM-based crypto driver`

### 1. 创建驱动目录结构

```
core/drivers/crypto/nuclei/
├── cipher.c          # AES/SM4 实现
├── hash.c            # MD5/SHA/SM3 实现
├── hmac.c            # HMAC 实现
├── rsa.c             # RSA 实现
├── mailbox.c         # HSM 通信驱动
├── nuclei_hsm_cryp.c # 初始化入口
├── nuclei_hsm_abi.h  # HSM 指令定义
├── common.h          # 公共接口声明
├── crypto.mk         # 编译配置
└── sub.mk            # 构建规则
```

### 2. 实现初始化入口

**`nuclei_hsm_cryp.c`** - 注册各算法到 drvcrypt 框架：

```c
#include "common.h"
#include <initcall.h>
#include <mm/core_mmu.h>

void* mailbox_base;
register_phys_mem(MEM_AREA_IO_SEC, MAILBOX_BASE, 0x4000);

static TEE_Result nuclei_hsm_cryp_init(void)
{
    TEE_Result res = TEE_SUCCESS;

    res = nuclei_register_cipher();  // 注册AES/SM4
    res = nuclei_register_hash();    // 注册MD5/SHA/SM3
    res = nuclei_register_hmac();    // 注册HMAC
    res = nuclei_register_rsa();     // 注册RSA
    
    // 映射 Mailbox 硬件内存
    mailbox_base = (void *)core_mmu_get_va(MAILBOX_BASE, 
                                           MEM_AREA_IO_SEC, 0x1000);
    return TEE_SUCCESS;
}

early_init(nuclei_hsm_cryp_init);  // 系统启动时自动调用
```

### 3. 实现算法驱动接口

**`cipher.c`** - 实现 `drvcrypt_cipher` 接口：

```c
static struct drvcrypt_cipher driver_cipher = {
    .alloc_ctx = nuclei_cipher_allocate,
    .free_ctx = nuclei_cipher_free,
    .init = nuclei_cipher_initialize,
    .update = nuclei_cipher_update,    // 核心：调用HSM
    .final = nuclei_cipher_final,
    .copy_state = nuclei_cipher_copy_state,
};

TEE_Result nuclei_register_cipher(void)
{
    return drvcrypt_register_cipher(&driver_cipher);
}
```

**`nuclei_cipher_update()` 核心实现**：

```c
static TEE_Result nuclei_cipher_update(struct drvcrypt_cipher_update *dupdate)
{
    // 1. 准备 HSM 命令描述符
    cipherdata->cmd_desc.cryp.header.opcode = SECURE_SERVICE_OPCODE_CRYP;
    cipherdata->cmd_desc.cryp.input_data_addr_low = 
        virt_to_phys(dupdate->src.data);
    
    // 2. Cache 同步（发送前 flush）
    cache_operation(TEE_CACHEFLUSH, dupdate->src.data, 
                    dupdate->src.length);
    
    // 3. 通过 Mailbox 发送命令
    mailbox_secure_service_host_send((uint32_t *)(&cipherdata->cmd_desc), 
                                     SECURE_SERVICE_OPCODE_CRYP, 
                                     mailbox_num);
    
    // 4. 接收结果
    mailbox_secure_service_host_receive(rbuf, mailbox_num);
    
    // 5. Cache 同步（接收后 invalidate）
    cache_operation(TEE_CACHEINVALIDATE, aligned_outbuf, outbuf_len);
    
    return TEE_SUCCESS;
}
```

### 4. 集成到编译系统

**`core/drivers/crypto/sub.mk`** - 注册子目录：
```makefile
subdirs-$(CFG_NUCLEI_HSM_CRYPTO) += nuclei
```

**`crypto.mk`** - 编译配置：
```makefile
ifeq ($(CFG_NUCLEI_HSM_CRYPTO),y)
$(call force,CFG_CRYPTO_DRIVER,y)  # 启用驱动框架
CFG_CRYPTO_DRIVER_DEBUG ?= 0
endif
```

**`plat-nuclei/conf.mk`** - 平台启用：
```makefile
$(call force,CFG_NUCLEI_HSM_CRYPTO,y)    # 启用 HSM 加密
$(call force,CFG_CRYPTO_DRV_CIPHER,y)    # 启用对称加密驱动
$(call force,CFG_CRYPTO_DRV_HASH,y)      # 启用哈希驱动
$(call force,CFG_CRYPTO_DRV_MAC,y)       # 启用 MAC 驱动
$(call force,CFG_CRYPTO_DRV_ACIPHER,y)   # 启用非对称加密驱动
$(call force,CFG_CRYPTO_DRV_RSA,y)       # 启用 RSA 驱动
```

### 5. 关键设计要点

1. **分层架构**：TEE Crypto API → drvcrypt 抽象层 → 硬件驱动 → Mailbox → HSM
2. **驱动注册**：使用 `early_init()` 在系统启动时自动注册到 drvcrypt
3. **内存管理**：`register_phys_mem()` 注册 IO 区域，`core_mmu_get_va()` 映射虚拟地址
4. **Cache 同步**：HSM 操作前后必须进行 Cache flush/invalidate
5. **错误处理**：检查 Mailbox 返回状态，超时返回 `TEE_ERROR_BUSY`
6. **地址转换**：使用 `virt_to_phys()` 将虚拟地址转为物理地址供 HSM 使用

---

## 注意事项

1. **PMP条目**: 框架需要至少4个PMP条目
2. **PLIC中断**: 安全中断号需要在系统层面规划
3. **内存对齐**: `mattri_base` 需要以共享区域大小对齐
4. **多核同步**: 使用 `mutex_lock()`/`mutex_unlock()` 保护共享数据
5. **TA限制**: 用户TA不能直接访问硬件，需要通过PTA
6. **Cache同步**: HSM操作前后需要 `cache_operation(TEE_CACHEFLUSH/INVALIDATE)`
7. **Mailbox超时**: HSM通信有超时机制，超时返回 `TEE_ERROR_BUSY`
