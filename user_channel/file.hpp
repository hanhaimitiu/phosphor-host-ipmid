#pragma once

/**
 * @file file.hpp
 * @brief user_channel 配置文件的路径与格式定义。
 */
/*  @file file.hpp
 *  @brief user_channel 配置文件的路径与格式定义。 */

#include <stdio.h>

#include <filesystem>
namespace phosphor
{
namespace user
{

namespace fs = std::filesystem;

/** @class File
 *  @brief Responsible for handling file pointer
 *  Needed by putspent(3)
 */
/* @class File
 *   @brief 负责处理文件指针
 *   putspent(3) 所需 */
class File
{
  private:
    /** @brief handler for operating on file */
    /* @brief 操作文件的处理器 */
    FILE* fp = nullptr;

    /** @brief File name. Needed in the case where the temp
     *         needs to be removed
     */
    /* @brief 文件名。在需要删除临时
     *         文件时会用到 */
    const std::string& name;

    /** @brief Should the file be removed at exit */
    /* @brief 退出时是否应删除该文件 */
    bool removeOnExit = false;

  public:
    File() = delete;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) = delete;
    File& operator=(File&&) = delete;

    /** @brief Opens file and uses it to do file operation
     *
     *  @param[in] name         - File name
     *  @param[in] mode         - File open mode
     *  @param[in] removeOnExit - File to be removed at exit or no
     */
    /* @brief 打开文件并用它执行文件操作
     *   @param[in] name         - 文件名
     *   @param[in] mode         - 文件打开模式
     *   @param[in] removeOnExit - 退出时是否删除该文件 */
    File(const std::string& name, const std::string& mode,
         bool removeOnExit = false) : name(name), removeOnExit(removeOnExit)
    {
        fp = fopen(name.c_str(), mode.c_str());
    }

    /** @brief Opens file using provided file descriptor
     *
     *  @param[in] fd           - File descriptor
     *  @param[in] name         - File name
     *  @param[in] mode         - File open mode
     *  @param[in] removeOnExit - File to be removed at exit or no
     */
    /* @brief 使用提供的文件描述符打开文件
     *   @param[in] fd           - 文件描述符
     *   @param[in] name         - 文件名
     *   @param[in] mode         - 文件打开模式
     *   @param[in] removeOnExit - 退出时是否删除该文件 */
    File(int fd, const std::string& name, const std::string& mode,
         bool removeOnExit = false) : name(name), removeOnExit(removeOnExit)
    {
        fp = fdopen(fd, mode.c_str());
    }

    ~File()
    {
        if (fp)
        {
            fclose(fp);
        }

        // Needed for exception safety
        // 异常安全所需
        if (removeOnExit && fs::exists(name))
        {
            fs::remove(name);
        }
    }

    auto operator()()
    {
        return fp;
    }
};

} // namespace user
} // namespace phosphor
