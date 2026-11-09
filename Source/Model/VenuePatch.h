#pragma once
#include <vector>
#include "FixtureInstance.h"

class VenuePatch
{
public:
    VenuePatch() = default;

    void addFixture(const FixtureInstance& fixture)
    {
        fixtures.push_back(fixture);
    }

    void clear()
    {
        fixtures.clear();
    }

    const std::vector<FixtureInstance>& getFixtures() const { return fixtures; }
    std::vector<FixtureInstance>& getFixtures() { return fixtures; }

private:
    std::vector<FixtureInstance> fixtures;
};