#pragma once

struct Region
{
    Region* parent = nullptr;

    bool outlives(const Region* o) const;
};