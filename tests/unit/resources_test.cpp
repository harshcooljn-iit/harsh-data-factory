#include <gtest/gtest.h>

#include "flowforge/resources/resource_pool.hpp"

namespace {

using Capacity = flowforge::domain::ResourcePool;
using flowforge::domain::ResourceRequirements;
using RuntimePool = flowforge::resources::ResourcePool;

ResourceRequirements cpu(int cores) {
    ResourceRequirements r;
    r.cpu_cores = cores;
    return r;
}

TEST(ResourcePool, ReservationNeverExceedsCapacity) {
    RuntimePool pool(Capacity{8, 0, 0});
    EXPECT_TRUE(pool.try_reserve(cpu(4)));   // A
    EXPECT_TRUE(pool.try_reserve(cpu(4)));   // B  -> full
    EXPECT_FALSE(pool.try_reserve(cpu(4)));  // C  -> must wait
    EXPECT_EQ(pool.available().total_cpu_cores, 0);

    pool.release(cpu(4));                   // A done
    EXPECT_TRUE(pool.try_reserve(cpu(4)));  // C can go now
}

TEST(ResourcePool, MemoryAndGpuAccounting) {
    RuntimePool pool(Capacity{16, 1024, 2});
    ResourceRequirements big;
    big.cpu_cores = 2;
    big.memory_mb = 768;
    big.gpu_count = 1;

    EXPECT_TRUE(pool.try_reserve(big));
    ResourceRequirements another = big;
    EXPECT_FALSE(pool.try_reserve(another));  // only 256MB / 1 GPU left
    another.memory_mb = 256;
    another.gpu_count = 1;
    EXPECT_TRUE(pool.try_reserve(another));
    EXPECT_EQ(pool.available().total_memory_mb, 0);
    EXPECT_EQ(pool.available().total_gpu_count, 0);
}

TEST(ResourcePool, DoubleReleaseCannotManufactureCapacity) {
    RuntimePool pool(Capacity{4, 0, 0});
    EXPECT_TRUE(pool.try_reserve(cpu(2)));
    pool.release(cpu(2));
    pool.release(cpu(2));  // stray release
    EXPECT_EQ(pool.available().total_cpu_cores, 4);
}

TEST(ResourcePool, CanEverFit) {
    RuntimePool pool(Capacity{4, 512, 0});
    EXPECT_TRUE(pool.can_ever_fit(cpu(4)));
    EXPECT_FALSE(pool.can_ever_fit(cpu(5)));
    ResourceRequirements gpu;
    gpu.gpu_count = 1;
    EXPECT_FALSE(pool.can_ever_fit(gpu));
}

}  // namespace
