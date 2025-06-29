export module GW2Viewer.Common.Hash;
import <picosha2.h>;

export struct Hasher
{
    picosha2::hash256_one_by_one SHA256;
};
