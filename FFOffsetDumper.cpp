#include "FFOffsetDumper.hpp"
#import <Foundation/Foundation.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

struct BytePat { uint8_t val; uint8_t mask; };

static std::vector<BytePat> parseIdaPattern(const std::string& s) {
    std::vector<BytePat> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        if (tok == "?" || tok == "??") out.push_back({0, 0});
        else {
            uint8_t b = (uint8_t)strtoul(tok.c_str(), nullptr, 16);
            out.push_back({b, 0xFF});
        }
    }
    return out;
}

static std::string appDocPath(const std::string& name) {
    @autoreleasepool {
        NSString* docs = [NSSearchPathForDirectoriesInDomains(
            NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        return std::string([docs UTF8String]) + "/" + name;
    }
}

bool FFOffsetDumper::readMem(uint64_t addr, void* buf, size_t len) {
    vm_size_t got = 0;
    kern_return_t kr = mach_vm_read_overwrite(mach_task_self(),
        (mach_vm_address_t)addr, (mach_vm_size_t)len,
        (mach_vm_address_t)buf, &got);
    return (kr == KERN_SUCCESS && got == len);
}

bool FFOffsetDumper::enumerateRegions() {
    regions_.clear();
    mach_vm_address_t addr = 0;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    vm_region_submap_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    while (true) {
        kern_return_t kr = mach_vm_region_recurse(mach_task_self(),
            &addr, &size, &depth, (vm_region_recurse_info_t)&info, &count);
        if (kr != KERN_SUCCESS) break;
        if (info.is_submap) { depth++; continue; }
        MemRegion r;
        r.start = addr; r.end = addr + size; r.prot = info.protection;
        regions_.push_back(r);
        addr += size;
    }
    return !regions_.empty();
}

bool FFOffsetDumper::parseImageBounds() {
    const struct mach_header_64* hdr =
        (const struct mach_header_64*)_dyld_get_image_header(0);
    if (!hdr) return false;
    slide_ = (uint64_t)_dyld_get_image_vmaddr_slide(0);
    imageBase_ = (uint64_t)hdr + slide_;
    const uint8_t* p = (const uint8_t*)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command* lc = (const struct load_command*)p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* seg = (const struct segment_command_64*)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) {
                imageSize_ = seg->vmsize;
                return true;
            }
        }
        p += lc->cmdsize;
    }
    return false;
}

uint64_t FFOffsetDumper::scanIdaPattern(uint64_t start, uint64_t end, const std::string& idaPat) {
    auto pat = parseIdaPattern(idaPat);
    if (pat.empty()) return 0;
    const size_t chunk = 0x10000;
    std::vector<uint8_t> buf(chunk + pat.size());
    for (uint64_t a = start; a + pat.size() < end; a += chunk) {
        size_t want = (size_t)std::min<uint64_t>(chunk + pat.size() - 1, end - a);
        if (!readMem(a, buf.data(), want)) continue;
        for (size_t i = 0; i + pat.size() <= want; i++) {
            bool ok = true;
            for (size_t j = 0; j < pat.size(); j++) {
                if ((buf[i + j] & pat[j].mask) != (pat[j].val & pat[j].mask)) { ok = false; break; }
            }
            if (ok) return a + i;
        }
    }
    return 0;
}

uint64_t FFOffsetDumper::findInitBase() {
    const char* builtin[] = {
        "FF 03 01 D1 FC 6F BA A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9",
        "FD 7B BF A9 FD 03 00 91 F4 4F BF A9 F6 57 BE A9 F8 5F 01 A9",
        "FF 43 01 D1 FC 6F 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9",
    };
    uint64_t textStart = 0, textEnd = 0;
    for (auto& r : regions_) {
        if ((r.prot & VM_PROT_EXECUTE) && r.start >= imageBase_ && r.start < imageBase_ + imageSize_) {
            if (!textStart || r.start < textStart) textStart = r.start;
            if (r.end > textEnd) textEnd = r.end;
        }
    }
    if (!textStart) return 0;
    for (auto& pat : builtin) {
        uint64_t hit = scanIdaPattern(textStart, textEnd, pat);
        if (hit) return hit;
    }
    return 0;
}

uint64_t FFOffsetDumper::resolveChain(uint64_t base, const std::vector<uint64_t>& offs) {
    uint64_t cur = base;
    for (auto off : offs) {
        uint64_t next = 0;
        if (!readMem(cur + off, &next, sizeof(next))) return 0;
        if (!next) return 0;
        cur = next;
    }
    return cur;
}

bool FFOffsetDumper::loadConfig(const std::string& path) {
    @autoreleasepool {
        NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
        if (!data) return false;
        NSError* err = nil;
        NSDictionary* root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&err];
        if (!root || err) return false;
        NSArray* patterns = root[@"patterns"];
        if (![patterns isKindOfClass:[NSArray class]]) return false;
        rules_.clear();
        for (NSDictionary* p in patterns) {
            PatternRule r;
            r.name = [p[@"name"] UTF8String] ?: "";
            r.pattern = [p[@"pattern"] UTF8String] ?: "";
            r.resolve_pointer_chain = [p[@"chain"] boolValue];
            NSArray* chain = p[@"offsets"];
            if ([chain isKindOfClass:[NSArray class]]) {
                for (NSNumber* n in chain) r.chain_offsets.push_back(n.unsignedLongLongValue);
            }
            if (!r.pattern.empty()) rules_.push_back(r);
        }
        return !rules_.empty();
    }
}

std::string FFOffsetDumper::toHex(uint64_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << v;
    return ss.str();
}

void FFOffsetDumper::writeReport(const std::string& path) {
    @autoreleasepool {
        NSMutableDictionary* out = [NSMutableDictionary dictionary];
        out[@"image_base"]  = @(toHex(imageBase_).c_str());
        out[@"slide"]       = @(toHex(slide_).c_str());
        out[@"init_base"]   = @(toHex(initBase_).c_str());
        out[@"init_offset"] = @(toHex(initBase_ - imageBase_).c_str());
        NSMutableArray* pats = [NSMutableArray array];
        for (auto& r : rules_) {
            uint64_t hit = scanIdaPattern(imageBase_, imageBase_ + imageSize_, r.pattern);
            NSMutableDictionary* d = [NSMutableDictionary dictionary];
            d[@"name"]    = @(r.name.c_str());
            d[@"pattern"] = @(r.pattern.c_str());
            d[@"found"]   = @(hit != 0);
            if (hit) {
                d[@"address"] = @(toHex(hit).c_str());
                d[@"offset"]  = @(toHex(hit - imageBase_).c_str());
                if (r.resolve_pointer_chain && !r.chain_offsets.empty()) {
                    uint64_t resolved = resolveChain(hit, r.chain_offsets);
                    if (resolved) d[@"chain_resolved"] = @(toHex(resolved).c_str());
                }
            }
            [pats addObject:d];
        }
        out[@"patterns"] = pats;
        NSData* json = [NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:[NSString stringWithUTF8String:path.c_str()] atomically:YES];
    }
}

FFOffsetDumper::FFOffsetDumper() {}
FFOffsetDumper::~FFOffsetDumper() {}

bool FFOffsetDumper::init() {
    if (!parseImageBounds()) return false;
    if (!enumerateRegions()) return false;
    return true;
}

void FFOffsetDumper::run() {
    NSLog(@"[FFDumper] image base = %s slide = %s",
          toHex(imageBase_).c_str(), toHex(slide_).c_str());
    loadConfig(appDocPath("config.json"));
    initBase_ = findInitBase();
    if (initBase_) {
        NSLog(@"[FFDumper] init base = %s offset = %s",
              toHex(initBase_).c_str(), toHex(initBase_ - imageBase_).c_str());
    } else {
        NSLog(@"[FFDumper] init base not found — add pattern to config.json");
    }
    writeReport(appDocPath("offsets.json"));
    NSLog(@"[FFDumper] report written");
}
