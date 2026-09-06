#include <doctest/doctest.h>

#include "base/pool.h"

namespace {
struct DummyTag;
using DummyHandle = Handle<DummyTag>;
}  // namespace

TEST_CASE("pool: acquire y resolve devuelven el mismo item") {
    Pool<i32, 4> pool;
    pool_init(&pool);

    i32*        item = nullptr;
    DummyHandle h    = pool_acquire<DummyTag>(&pool, &item);
    REQUIRE(h.valid());
    *item = 42;

    i32* resolved = pool_resolve<DummyTag>(&pool, h);
    REQUIRE(resolved != nullptr);
    CHECK(*resolved == 42);
}

TEST_CASE("pool: un handle caducado tras liberar y reutilizar el slot no resuelve") {
    Pool<i32, 1> pool;
    pool_init(&pool);

    DummyHandle h1 = pool_acquire<DummyTag>(&pool);
    pool_release<DummyTag>(&pool, h1);
    DummyHandle h2 = pool_acquire<DummyTag>(&pool);

    CHECK(h1.index == h2.index);
    CHECK(h1.gen != h2.gen);
    CHECK(pool_resolve<DummyTag>(&pool, h1) == nullptr);
    CHECK(pool_resolve<DummyTag>(&pool, h2) != nullptr);
}

TEST_CASE("pool: acquire devuelve handle nulo cuando el pool esta lleno") {
    Pool<i32, 1> pool;
    pool_init(&pool);

    DummyHandle h1 = pool_acquire<DummyTag>(&pool);
    CHECK(h1.valid());

    DummyHandle h2 = pool_acquire<DummyTag>(&pool);
    CHECK_FALSE(h2.valid());
}
