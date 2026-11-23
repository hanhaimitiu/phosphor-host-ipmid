#pragma once

/**
 * @file storageaddsel.hpp
 * @brief SEL 添加事件的声明。
 */
/* @file storageaddsel.hpp
 * @brief SEL 添加事件的声明。 */

#include <stdint.h>

#include <string>

/** @brief Read eSEL data into a string
 *
 *  @param[in] filename - filename of file containing eSEL
 *
 *  @return On success return the eSEL data
 */
/* @brief 将 eSEL 数据读入字符串
 *  @param[in] filename - 包含 eSEL 的文件名
 *  @return 成功时返回 eSEL 数据 */
std::string readESEL(const char* filename);

/** @brief Create a log entry with maintenance procedure
 *
 *  @param[in] procedureNum - procedure number associated with the log entry
 */
/* @brief 创建带维护过程的日志条目
 *  @param[in] procedureNum - 与该日志条目关联的过程编号 */
void createProcedureLogEntry(uint8_t procedureNum);
