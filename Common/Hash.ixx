export module GW2Viewer.Common.Hash;
import <picosha2.h>;

export namespace GW2Viewer
{

struct Hasher
{
    picosha2::hash256_one_by_one SHA256;
};

}
