
/**
 * @file serialbridged.cpp
 * @brief 串口桥守护进程：在串口上转发 IPMI 报文。
 */
/*  @file serialbridged.cpp
 *  @brief 串口桥守护进程：在串口上转发 IPMI 报文。 */
#include "serialcmd.hpp"

#include <systemd/sd-daemon.h>

#include <CLI/CLI.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/slot.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/source/io.hpp>
#include <sdeventplus/source/signal.hpp>
#include <stdplus/exception.hpp>
#include <stdplus/fd/create.hpp>
#include <stdplus/fd/ops.hpp>
#include <stdplus/signal.hpp>

namespace serialbridge
{

using sdeventplus::source::IO;
using sdeventplus::source::Signal;
using stdplus::fd::OpenAccess;
using stdplus::fd::OpenFlag;
using stdplus::fd::OpenFlags;

int execute(const std::string& channel, const bool& verbose)
{
    // Set up DBus and event loop
    // 设置 DBus 和事件循环
    auto event = sdeventplus::Event::get_default();
    auto bus = sdbusplus::bus::new_default();
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    // Configure basic signal handling
    // 配置基本的信号处理
    auto exit_handler = [&event](Signal&, const struct signalfd_siginfo*) {
        lg2::error("Interrupted, Exiting\n");
        event.exit(0);
    };
    stdplus::signal::block(SIGINT);
    Signal sig_init(event, SIGINT, exit_handler);
    stdplus::signal::block(SIGTERM);
    Signal sig_term(event, SIGTERM, exit_handler);

    // Open an FD for the UART channel
    // 为 UART 通道打开一个 FD
    stdplus::ManagedFd uart = stdplus::fd::open(
        std::format("/dev/{}", channel.c_str()),
        OpenFlags(OpenAccess::ReadWrite).set(OpenFlag::NonBlock));
    sdbusplus::slot slot(nullptr);

    std::unique_ptr<SerialChannel> serialchannel =
        std::make_unique<SerialChannel>(verbose);

    // Add a reader to the bus for handling inbound IPMI
    // 在 bus 上添加 reader，用于处理传入的 IPMI 报文
    IO ioSource(event, uart.get(), EPOLLIN | EPOLLET,
                stdplus::exception::ignore(
                    [&serialchannel, &uart, &bus, &slot](IO&, int, uint32_t) {
                        serialchannel->read(uart, bus, slot);
                    }));

    sd_notify(0, "READY=1");
    return event.loop();
}

} // namespace serialbridge

int main(int argc, char* argv[])
{
    std::string device;
    bool verbose = 0;

    // Parse input parameter
    // 解析输入参数
    CLI::App app("Serial IPMI Bridge");
    app.add_option("-d,--device", device, "select uart device");
    app.add_option("-v,--verbose", verbose, "enable debug message");
    CLI11_PARSE(app, argc, argv);

    try
    {
        return serialbridge::execute(device, verbose);
    }
    catch (const std::exception& e)
    {
        lg2::error("FAILED: {MSG}\n", "MSG", e);
        return 1;
    }
}
