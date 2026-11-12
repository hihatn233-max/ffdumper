#pragma once
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <string>
#include <vector>
#include <cstdint>

struct MemRegion { uint64_t start; uint64_t end; uint32_t prot; };
struct PatternRule {
    std::string name;
    std::string pattern;
    bool resolve_pointer_chain;
    std::vector<uint64_t> chain_offsets;
};

class FFOffsetDumper {
public:
    FFOffsetDumper();
    ~FFOffsetDumper();
    bool init();
    bool loadConfig(const std::string& path);
    void run();
private:
    uint64_t imageBase_ = 0, imageSize_ = 0, slide_ = 0, initBase_ = 0;
    std::vector<MemRegion> regions_;
    std::vector<PatternRule> rules_;
    bool enumerateRegions();
    bool parseImageBounds();
    bool readMem(uint64_t addr, void* buf, size_t len);
    uint64_t scanIdaPattern(uint64_t start, uint64_t end, const std::string& pat);
    uint64_t findInitBase();
    uint64_t resolveChain(uint64_t base, const std::vector<uint64_t>& offs);
    void writeReport(const std::string& path);
    std::string toHex(uint64_t v);
};
