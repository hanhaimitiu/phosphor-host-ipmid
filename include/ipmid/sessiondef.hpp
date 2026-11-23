
/**
 * @file sessiondef.hpp
 * @brief IPMI 会话/激活相关常量定义。
 */
/*  @file sessiondef.hpp
 *  @brief IPMI 会话/激活相关常量定义。 */
/*
 * Copyright © 2019 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/*  版权所有 © 2019 Intel Corporation
 *  根据 Apache 许可证 2.0 版（"License"）授权；
 *  除非符合许可证的规定，否则不得使用本文件。
 *  你可以从以下地址获取许可证副本：
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  除非适用法律要求或书面同意，否则本软件
 *  按"原样"分发，
 *  不提供任何明示或暗示的保证或条件。
 *  有关许可证下的具体权限和限制，请参阅许可证
 *  本身。 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace session
{

static constexpr auto sessionManagerRootPath =
    "/xyz/openbmc_project/ipmi/session";
static constexpr auto sessionIntf = "xyz.openbmc_project.Ipmi.SessionInfo";
static constexpr uint8_t ipmi20VerSession = 0x01;
static constexpr size_t maxSessionCountPerChannel = 15;
static constexpr size_t sessionZero = 0;
static constexpr size_t maxSessionlessCount = 1;
static constexpr uint8_t invalidSessionID = 0;
static constexpr uint8_t invalidSessionHandle = 0;
static constexpr uint8_t defaultSessionHandle = 0xFF;
static constexpr uint8_t maxNetworkInstanceSupported = 4;
static constexpr uint8_t ccInvalidSessionId = 0x87;
static constexpr uint8_t ccInvalidSessionHandle = 0x88;
static constexpr uint8_t searchCurrentSession = 0;
static constexpr uint8_t searchSessionByHandle = 0xFE;
static constexpr uint8_t searchSessionById = 0xFF;
// MSB BIT 7 BIT 6 assigned for netipmid instance in session handle.
// 会话句柄中的 MSB BIT 7、BIT 6 分配给 netipmid 实例。
static constexpr uint8_t multiIntfaceSessionHandleMask = 0x3F;

// MSB BIT 31-BIT30 assigned for netipmid instance in session ID
// 会话 ID 中的 MSB BIT 31-BIT30 分配给 netipmid 实例
static constexpr uint32_t multiIntfaceSessionIDMask = 0x3FFFFFFF;

enum class State : uint8_t
{
    inactive,           // Session is not in use
    setupInProgress,    // Session Setup Sequence is progressing
    active,             // Session is active
    tearDownInProgress, // When Closing Session
};

} // namespace session
