// Independent engine-side reference values for the SCUA binding comparisons.
#include "math/MathModule.h"
#include "math/RandomGenerator.h"
#include "math/Transform.h"
#include <iomanip>
#include <iostream>
#include <vector>
int main()
{
    using namespace love::math;
    std::cout << std::setprecision(17);
    auto array = [](const char *name, const std::vector<double> &values)
    {
        std::cout << '"' << name << "\":[";
        for (size_t i = 0; i < values.size(); ++i)
            std::cout << (i ? "," : "") << values[i];
        std::cout << ']';
    };
    std::cout << '{';
    array("simplex", {simplexNoise1(.125), simplexNoise2(.125, -.25), simplexNoise3(.125, -.25, .375),
                      simplexNoise4(.125, -.25, .375, .5)});
    std::cout << ',';
    array("perlin", {perlinNoise1(.125), perlinNoise2(.125, -.25), perlinNoise3(.125, -.25, .375),
                     perlinNoise4(.125, -.25, .375, .5)});
    RandomGenerator random;
    RandomGenerator::Seed seed{};
    seed.b32.low = 0xfedcba98u;
    seed.b32.high = 0x89abcdefu;
    random.setSeed(seed);
    std::vector<double> sequence;
    for (int i = 0; i < 8; ++i)
        sequence.push_back(random.random());
    std::cout << ',';
    array("random", sequence);
    random.setSeed(seed);
    std::cout << ',';
    array("normal", {random.randomNormal(1), random.randomNormal(1)});
    Transform transform(3, -4, .3f, 2, -3, 1, 2, .1f, -.2f);
    auto point = transform.transformPoint({5, 6});
    std::cout << ',';
    array("point", {point.x, point.y});
    const auto data = transform.getMatrix().getElements();
    std::vector<double> matrix;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix.push_back(data[column * 4 + row]);
    std::cout << ',';
    array("matrix", matrix);
    std::cout << "}\n";
}
