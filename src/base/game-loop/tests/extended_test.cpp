// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "../src/extended.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
using oa::base::game_loop::detail::Extended;
using oa::base::game_loop::detail::Wide;

struct Fixture {
    uint64_t a{}, b{}, c{}, result64{};
    uint32_t result32{};
    uint64_t result53_64{};
    uint32_t result53_32{};
};

// Expected values computed independently with Python Fraction exact rationals,
// nearest-even quantization to64or53significand bits after multiply/add, then IEEE
// binary64/32 (including subnormal) rounding. Includes cancellation and ties.
constexpr Fixture fixtures[]{
    {0x3ff0000000000001ULL,
     0x3ff0000000000001ULL,
     0xbff0000000000000ULL,
     0x3cc0000000000000ULL,
     0x26000000U,
     0x3cc0000000000000ULL,
     0x26000000U},
    {0x3ff0000000000000ULL,
     0x3ff0000000000000ULL,
     0x3bf0000000000000ULL,
     0x3ff0000000000000ULL,
     0x3f800000U,
     0x3ff0000000000000ULL,
     0x3f800000U},
    {0x3ff0000000000000ULL,
     0x3ff0000000000000ULL,
     0x3c08000000000000ULL,
     0x3ff0000000000000ULL,
     0x3f800000U,
     0x3ff0000000000000ULL,
     0x3f800000U},
    {0x3ff0000000000000ULL,
     0x3ff0000000000000ULL,
     0xbbe0000000000000ULL,
     0x3ff0000000000000ULL,
     0x3f800000U,
     0x3ff0000000000000ULL,
     0x3f800000U},
    {0x43e0000000000000ULL,
     0x3ff0000000000000ULL,
     0x3ff0000000000000ULL,
     0x43e0000000000000ULL,
     0x5f000000U,
     0x43e0000000000000ULL,
     0x5f000000U},
    {0xc3e0000000000000ULL,
     0x3ff0000000000000ULL,
     0xbff0000000000000ULL,
     0xc3e0000000000000ULL,
     0xdf000000U,
     0xc3e0000000000000ULL,
     0xdf000000U},
    {0x36a0000000000000ULL,
     0x3ff0000000000000ULL,
     0x0000000000000000ULL,
     0x36a0000000000000ULL,
     0x00000001U,
     0x36a0000000000000ULL,
     0x00000001U},
    {0x3690000000000000ULL,
     0x3ff0000000000000ULL,
     0x0000000000000000ULL,
     0x3690000000000000ULL,
     0x00000000U,
     0x3690000000000000ULL,
     0x00000000U},
    {0x36a8000000000000ULL,
     0x3ff0000000000000ULL,
     0x0000000000000000ULL,
     0x36a8000000000000ULL,
     0x00000002U,
     0x36a8000000000000ULL,
     0x00000002U},
    {0x3810000000000000ULL,
     0x3ff0000000000000ULL,
     0xb690000000000000ULL,
     0x380fffffe0000000ULL,
     0x00800000U,
     0x380fffffe0000000ULL,
     0x00800000U},
    {0x3810000000000000ULL,
     0x3ff0000000000000ULL,
     0xb6a8000000000000ULL,
     0x380fffffa0000000ULL,
     0x007ffffeU,
     0x380fffffa0000000ULL,
     0x007ffffeU},
    {0x0010000000000000ULL,
     0x3ff0000000000000ULL,
     0x8000000000000001ULL,
     0x000fffffffffffffULL,
     0x00000000U,
     0x000fffffffffffffULL,
     0x00000000U},
    {0x0000000000000001ULL,
     0x3ff0000000000000ULL,
     0x0000000000000000ULL,
     0x0000000000000001ULL,
     0x00000000U,
     0x0000000000000001ULL,
     0x00000000U},
    {0x0000000000000001ULL,
     0x3fe0000000000000ULL,
     0x0000000000000000ULL,
     0x0000000000000000ULL,
     0x00000000U,
     0x0000000000000000ULL,
     0x00000000U},
    {0xc19af79c73333334ULL,
     0x407dd11eb851eb85ULL,
     0x3fc1284b17bb2dacULL,
     0xc22920a0e1ae0101ULL,
     0xd1490507U,
     0xc22920a0e1ae0101ULL,
     0xd1490507U},
    {0xc1a1b4572b333333ULL,
     0x40749ca3d70a3d71ULL,
     0x3fe421a096ac39b4ULL,
     0xc226cec0930ea13aULL,
     0xd1367605U,
     0xc226cec0930ea13aULL,
     0xd1367605U},
    {0xc1946deca599999aULL,
     0x406263d70a3d70a4ULL,
     0x3f9e4611a53d6580ULL,
     0xc2077b24f4bc4eb8ULL,
     0xd03bd928U,
     0xc2077b24f4bc4eb8ULL,
     0xd03bd928U},
    {0x419ab06c12000000ULL,
     0x4064228f5c28f5c3ULL,
     0x3fed2432bf56128aULL,
     0x4210cb168f57152bULL,
     0x508658b4U,
     0x4210cb168f57152aULL,
     0x508658b4U},
    {0x41a56833dacccccdULL,
     0x408344a3d70a3d71ULL,
     0x3fd4c4bbabf29148ULL,
     0x4239c793a57b9dd4ULL,
     0x51ce3c9dU,
     0x4239c793a57b9dd4ULL,
     0x51ce3c9dU},
    {0xc18f1cdcdb333334ULL,
     0x40793947ae147ae1ULL,
     0x3feaf778ed045c83ULL,
     0xc218863da5edf509ULL,
     0xd0c431edU,
     0xc218863da5edf509ULL,
     0xd0c431edU},
    {0xc1a977f58899999aULL,
     0x4068775c28f5c290ULL,
     0x3fb0f62c626abc98ULL,
     0xc22378f799028168ULL,
     0xd11bc7bdU,
     0xc22378f799028169ULL,
     0xd11bc7bdU},
    {0x417ebd4688000000ULL,
     0x4075cc7ae147ae15ULL,
     0x3fdc81668e1c416eULL,
     0x4204f0a2f4eb29c7ULL,
     0x50278518U,
     0x4204f0a2f4eb29c7ULL,
     0x50278518U},
    {0x411f9ea0cccccccdULL,
     0x407a30a3d70a3d71ULL,
     0x3fe795f61337042fULL,
     0x41a9e0f27d91f2d6ULL,
     0x4d4f0794U,
     0x41a9e0f27d91f2d6ULL,
     0x4d4f0794U},
    {0x417bb300f4cccccdULL,
     0x407bf07ae147ae15ULL,
     0x3fa431af73461a10ULL,
     0x42082f31b32d6d73ULL,
     0x5041798eU,
     0x42082f31b32d6d74ULL,
     0x5041798eU},
    {0x41a12e9b82cccccdULL,
     0x40671d1eb851eb85ULL,
     0x3fdcfde2e00f3c38ULL,
     0x4218d2450338d3f7ULL,
     0x50c69228U,
     0x4218d2450338d3f7ULL,
     0x50c69228U},
    {0xc17b76eb14cccccdULL,
     0x40766fd70a3d70a4ULL,
     0x3fef197459447a1bULL,
     0xc20341bead7a856aULL,
     0xd01a0df5U,
     0xc20341bead7a856aULL,
     0xd01a0df5U},
    {0x4175b4123999999aULL,
     0x4072533333333333ULL,
     0x3fea452da0e6f118ULL,
     0x41f8db7012d2414fULL,
     0x4fc6db81U,
     0x41f8db7012d24150ULL,
     0x4fc6db81U},
    {0xc17fb8fff199999aULL,
     0x403bab851eb851ecULL,
     0x3fdf9d9917e15ab2ULL,
     0xc1cb6e2082a95843ULL,
     0xce5b7104U,
     0xc1cb6e2082a95844ULL,
     0xce5b7104U},
    {0x419c10dfb999999aULL,
     0x407522147ae147aeULL,
     0x3fa8441769ee05d0ULL,
     0x422288f6b5d3d6bbULL,
     0x511447b6U,
     0x422288f6b5d3d6bbULL,
     0x511447b6U},
    {0x419d87470399999aULL,
     0x406938a3d70a3d71ULL,
     0x3fe833e69c73245aULL,
     0x421745f3669965b9ULL,
     0x50ba2f9bU,
     0x421745f3669965b9ULL,
     0x50ba2f9bU},
    {0x4163ebeb43333334ULL,
     0x4058a00000000000ULL,
     0x3fec8ca54ceb8796ULL,
     0x41cea91815deff63ULL,
     0x4e7548c1U,
     0x41cea91815deff63ULL,
     0x4e7548c1U},
    {0xc195b37ccacccccdULL,
     0x407cbc7ae147ae15ULL,
     0x3fc9456cc10f10a8ULL,
     0xc2237cdf03aca83bULL,
     0xd11be6f8U,
     0xc2237cdf03aca83aULL,
     0xd11be6f8U},
    {0x41a19b4628666667ULL,
     0x4071b00000000000ULL,
     0x3fd756c3f16b1c9aULL,
     0x422376a68ea9edeaULL,
     0x511bb534U,
     0x422376a68ea9edeaULL,
     0x511bb534U},
    {0x41a728b9d3cccccdULL,
     0x404f570a3d70a3d7ULL,
     0x3fa91622fd2eac20ULL,
     0x4206ae722797d915ULL,
     0x50357391U,
     0x4206ae722797d916ULL,
     0x50357391U},
    {0xc14ecb684ccccccdULL,
     0x403d400000000000ULL,
     0x3fe33073686e75e3ULL,
     0xc19c25ed53cd24c6ULL,
     0xcce12f6bU,
     0xc19c25ed53cd24c6ULL,
     0xcce12f6bU},
    {0x419d9474fd333334ULL,
     0x40800a6666666666ULL,
     0x3fdc2aed5ed75860ULL,
     0x422da7af160bdc39ULL,
     0x516d3d79U,
     0x422da7af160bdc38ULL,
     0x516d3d79U},
    {0x41a18f23fa99999aULL,
     0x4082f251eb851eb8ULL,
     0x3fde77e96b78a3a0ULL,
     0x4234caf7613a368cULL,
     0x51a657bbU,
     0x4234caf7613a368cULL,
     0x51a657bbU},
    {0x4194d660df99999aULL,
     0x40803f851eb851ecULL,
     0x3fe9ac981db2b18fULL,
     0x4225291a5b12e58bULL,
     0x512948d3U,
     0x4225291a5b12e58cULL,
     0x512948d3U},
    {0x3ffb77533d69344fULL,
     0x3ffd0d102c2c1fecULL,
     0x0000000000000000ULL,
     0x4008ef59cb0bdac6ULL,
     0x40477aceU,
     0x4008ef59cb0bdac7ULL,
     0x40477aceU},
};

void require(bool ok, const char* error) {
    if (!ok)
        throw std::runtime_error(error);
}

int main() {
    try {
        require(
            std::bit_cast<uint64_t>((Extended(0.0) + Extended(-0.0)).to_double().value()) == 0,
            "opposite signed zeros"
        );
        require(
            std::bit_cast<uint64_t>((Extended(-0.0) + Extended(-0.0)).to_double().value()) ==
                0x8000000000000000ULL,
            "both negative zeros"
        );
        require(
            std::bit_cast<uint64_t>((Extended(-0.0) - Extended(-0.0)).to_double().value()) == 0,
            "zero cancellation"
        );
        require(
            !Extended(std::numeric_limits<double>::infinity()).to_double() &&
                !Extended(std::numeric_limits<double>::quiet_NaN()).to_float(),
            "nonfinite input refused by the conversions"
        );
        require(!Extended(1.0, 32).to_double(), "unsupported precision refused");
        require(
            !(Extended(1.0) + Extended(std::numeric_limits<double>::infinity()) * Extended(2.0))
                 .to_double(),
            "arithmetic carries a value that is not finite"
        );
        const Extended largest(std::numeric_limits<double>::max());
        require(!(largest * Extended(2.0)).to_double(), "binary64 overflow refused");
        require(!largest.to_float(), "binary32 overflow refused");
        auto product = Wide::multiply(0xffffffffffffffffULL, 0xffffffffffffffffULL);
        require(product.hi == 0xfffffffffffffffeULL && product.lo == 1, "wide full carry product");
        product = Wide::multiply(0x00000000ffffffffULL, 0x00000000ffffffffULL);
        require(product.hi == 0 && product.lo == 0xfffffffe00000001ULL, "wide low limb product");
        const Wide value{0x8000000000000001ULL, 0x8000000000000001ULL};
        const std::array<unsigned, 8> shifts{0, 1, 63, 64, 65, 127, 128, 129};
        const std::array<Wide, 8> jammed{
            {{0x8000000000000001ULL, 0x8000000000000001ULL},
             {0x4000000000000000ULL, 0xc000000000000001ULL},
             {1, 3},
             {0, 0x8000000000000001ULL},
             {0, 0x4000000000000001ULL},
             {0, 1},
             {0, 1},
             {0, 1}}
        };
        for (std::size_t i = 0; i < shifts.size(); ++i) {
            const auto got = value.right_jam(shifts[i]);
            require(got.hi == jammed[i].hi && got.lo == jammed[i].lo, "wide jam boundary");
        }
        auto carried = Wide{3, 0xffffffffffffffffULL}.add({5, 1});
        require(carried.hi == 9 && carried.lo == 0, "wide add carry");
        auto borrowed = Wide{9, 0}.subtract({5, 1});
        require(borrowed.hi == 3 && borrowed.lo == 0xffffffffffffffffULL, "wide subtract borrow");
        for (const auto& v : fixtures) {
            const auto got =
                Extended(std::bit_cast<double>(v.a)) * Extended(std::bit_cast<double>(v.b)) +
                Extended(std::bit_cast<double>(v.c));
            require(
                std::bit_cast<uint64_t>(got.to_double().value()) == v.result64, "extended64 golden"
            );
            require(
                std::bit_cast<uint32_t>(got.to_float().value()) == v.result32, "extended32 golden"
            );
            const auto got53 = Extended(std::bit_cast<double>(v.a), 53) *
                                   Extended(std::bit_cast<double>(v.b), 53) +
                               Extended(std::bit_cast<double>(v.c), 53);
            require(
                std::bit_cast<uint64_t>(got53.to_double().value()) == v.result53_64,
                "precision53 binary64 golden"
            );
            require(
                std::bit_cast<uint32_t>(got53.to_float().value()) == v.result53_32,
                "precision53 binary32 golden"
            );
        }
        std::cout << "extended arithmetic vectors passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
