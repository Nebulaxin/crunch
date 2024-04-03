#ifndef options_hpp
#define options_hpp

enum class BinaryStringFormat : char
{
    NullTerminated = 0,
    Prefix16 = 1,
    Prefix7 = 2
};

struct Options
{
    bool xml = false;
    bool json = false;
    bool binary = false;

    int width = 4096;
    int height = 4096;
    int padding = 1;

    bool premultiply = false;
    bool unique = false;
    bool trim = false;
    bool rotate = false;

    BinaryStringFormat binaryStringFormat = BinaryStringFormat::NullTerminated;
    bool force = false;
    bool verbose = false;
    bool useTimeForHash = false;
    bool splitSubdirectories = false;
    bool noZero = false;
};

extern Options options;

#endif
