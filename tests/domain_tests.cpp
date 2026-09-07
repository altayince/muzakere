#include "muz/domain/model.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Display filename normalization preserves UTF-8 and removes unsafe characters", "[unit]") {
    REQUIRE(muz::normalize_filename("file:name?.pdf") == "file_name_.pdf");
    REQUIRE(muz::normalize_filename("../a\\b. ") == ".._a_b");
    REQUIRE(muz::normalize_filename("...") == "unnamed");
    REQUIRE(muz::normalize_filename("\xc4\xb0" "cra.pdf") == "\xc4\xb0" "cra.pdf");
}

TEST_CASE("SHA-256 validation accepts only canonical lowercase digests", "[unit]") {
    REQUIRE(muz::valid_sha256(std::string(64, 'a')));
    REQUIRE_FALSE(muz::valid_sha256(std::string(63, 'a')));
    REQUIRE_FALSE(muz::valid_sha256(std::string(64, 'G')));
    REQUIRE_FALSE(muz::valid_sha256(std::string(64, 'A')));
}
