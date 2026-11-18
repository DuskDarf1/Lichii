// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "common/memory_detect.h"
#include "video_core/texture_cache/host_memory_limit.h"

namespace {

constexpr u64 MiB = u64{1} << 20;
constexpr u64 GiB = u64{1} << 30;

} // namespace

TEST_CASE("Host memory with room to spare does not bound the texture cache",
          "[video_core][texture_cache][host_memory_limit]") {
    // 32 GiB of RAM and a 16 GB card: far above the 9.7 GiB its device budget allows.
    REQUIRE(VideoCommon::HostCriticalMemory(6 * GiB, 20 * GiB) == 24 * GiB);
    REQUIRE(VideoCommon::HostCriticalMemory(6 * GiB, 20 * GiB) > 9723 * MiB);
}

TEST_CASE("The texture cache collects before the host runs out of commit",
          "[video_core][texture_cache][host_memory_limit]") {
    // The RX 7600 XT session: thresholds at 8955 / 9723 MiB from the device budget alone, on
    // a host with a 31.9 GiB commit limit that the guest's memory takes 14 GiB of.
    const u64 critical = VideoCommon::HostCriticalMemory(7 * GiB, 1 * GiB);
    REQUIRE(critical == 6 * GiB);
    REQUIRE(critical < 7 * GiB);
    // Freeing device memory gives the commit back, which leaves the threshold where it was.
    REQUIRE(VideoCommon::HostCriticalMemory(5 * GiB, 3 * GiB) == critical);
    // Commit taken by something else lowers it.
    REQUIRE(VideoCommon::HostCriticalMemory(7 * GiB, 512 * MiB) < critical);
}

TEST_CASE("The texture cache keeps what a scene needs when the host is out of commit",
          "[video_core][texture_cache][host_memory_limit]") {
    REQUIRE(VideoCommon::HostCriticalMemory(4 * GiB, 0) == VideoCommon::MinHostCriticalMemory);
    REQUIRE(VideoCommon::HostCriticalMemory(0, 0) == VideoCommon::MinHostCriticalMemory);
    REQUIRE(VideoCommon::HostCriticalMemory(1 * GiB, 1 * GiB) ==
            VideoCommon::MinHostCriticalMemory);
}

TEST_CASE("The host memory bound does not wrap", "[video_core][texture_cache][host_memory_limit]") {
    constexpr u64 max = (std::numeric_limits<u64>::max)();
    REQUIRE(VideoCommon::HostCriticalMemory(8 * GiB, max) == max - VideoCommon::HostCommitReserve);
    REQUIRE(VideoCommon::HostCriticalMemory(max, max) == max - VideoCommon::HostCommitReserve);
}

TEST_CASE("A job object's commit limit bounds the texture cache below the system's room",
          "[video_core][texture_cache][host_memory_limit]") {
    // 6 GiB of device memory with 20 GiB of system commit left, in a job that may commit 4 GiB
    // more: the job's room is the one an allocation fails past.
    const u64 system = VideoCommon::HostCriticalMemory(6 * GiB, 20 * GiB);
    const u64 job = VideoCommon::HostCriticalMemory(6 * GiB, 4 * GiB);
    REQUIRE(job == 8 * GiB);
    REQUIRE(job < system);
}

TEST_CASE("The process's commit room is reported where there is a commit limit",
          "[video_core][texture_cache][host_memory_limit]") {
    const std::optional<u64> room = Common::GetProcessCommitRoom();
#ifdef _WIN32
    REQUIRE(room.has_value());
    const Common::ProcessMemoryUsage usage = Common::GetProcessMemoryUsage();
    REQUIRE(*room <= usage.system_commit_limit);
    // Never more than the system's own room (with slack for commit that changed between the two
    // reads), so the thresholds are never above what the system-wide room gave.
    const u64 system_room = usage.system_commit_limit - usage.system_commit + 256 * MiB;
    REQUIRE(*room <= system_room);
    REQUIRE(VideoCommon::HostCriticalMemory(usage.commit, *room) <=
            VideoCommon::HostCriticalMemory(usage.commit, system_room));
#else
    REQUIRE_FALSE(room.has_value());
#endif
}
