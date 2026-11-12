#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <substrate.h>
#import <mach/mach.h>
#import <QuartzCore/QuartzCore.h>
#include <cmath>

// Cấu trúc đọc RAM
bool ReadMem(uint64_t addr, void* buf, size_t len) {
    vm_size_t got = 0;
    kern_return_t kr = mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)addr, (mach_vm_size_t)len, (mach_vm_address_t)buf, &got);
    return (kr == KERN_SUCCESS && got == len);
}

// Ma trận View
struct Matrix4x4 { float m[4][4]; };
struct Vector3 { float x, y, z; };

Matrix4x4 g_ViewMatrix;
uint64_t g_ViewMatrixAddr = 0;
uint64_t g_EntityListAddr = 0;
uint64_t g_LocalPlayerAddr = 0;

bool WorldToScreen(Vector3 pos, Vector3* screen, CGSize screenSize) {
    Vector3 camRight = {g_ViewMatrix.m[0][0], g_ViewMatrix.m[0][1], g_ViewMatrix.m[0][2]};
    Vector3 camUp    = {g_ViewMatrix.m[1][0], g_ViewMatrix.m[1][1], g_ViewMatrix.m[1][2]};
    Vector3 camDir   = {g_ViewMatrix.m[2][0], g_ViewMatrix.m[2][1], g_ViewMatrix.m[2][2]};
    Vector3 camPos   = {g_ViewMatrix.m[3][0], g_ViewMatrix.m[3][1], g_ViewMatrix.m[3][2]};
    
    Vector3 delta = {pos.x - camPos.x, pos.y - camPos.y, pos.z - camPos.z};
    float w = delta.x * camDir.x + delta.y * camDir.y + delta.z * camDir.z;
    if (w < 0.1f) return false; // Nằm sau lưng
    
    float x = delta.x * camRight.x + delta.y * camRight.y + delta.z * camRight.z;
    float y = delta.x * camUp.x + delta.y * camUp.y + delta.z * camUp.z;
    
    screen->x = (screenSize.width / 2.0f) + (x / w) * (screenSize.width / 2.0f);
    screen->y = (screenSize.height / 2.0f) - (y / w) * (screenSize.height / 2.0f);
    return true;
}

// --- OVERLAY UI & ESP ---

@interface ESPOverlay : UIView
@property (nonatomic, strong) CADisplayLink *link;
@property (nonatomic, assign) BOOL espEnabled;
@property (nonatomic, assign) BOOL aimbotEnabled;
@end

@implementation ESPOverlay

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];
        self.userInteractionEnabled = NO; // Xuyên qua màn hình
        _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick)];
        [_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        
        // Tạo menu nhỏ (nút bấm bật/tắt)
        [self setupMenu];
    }
    return self;
}

- (void)setupMenu {
    UIStackView *stack = [[UIStackView alloc] initWithFrame:CGRectMake(20, 100, 120, 100)];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 10;
    stack.userInteractionEnabled = YES;
    
    UISwitch *espSwitch = [[UISwitch alloc] init];
    [espSwitch addTarget:self action:@selector(toggleESP:) forControlEvents:UIControlEventValueChanged];
    
    UISwitch *aimSwitch = [[UISwitch alloc] init];
    [aimSwitch addTarget:self action:@selector(toggleAim:) forControlEvents:UIControlEventValueChanged];
    
    [stack addArrangedSubview:espSwitch];
    [stack addArrangedSubview:aimSwitch];
    [self addSubview:stack];
}

- (void)toggleESP:(UISwitch*)sender { self.espEnabled = sender.isOn; }
- (void)toggleAim:(UISwitch*)sender { self.aimbotEnabled = sender.isOn; }

- (void)tick {
    [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect {
    if (!self.espEnabled && !self.aimbotEnabled) return;
    if (!g_EntityListAddr || !g_ViewMatrixAddr) return;
    
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGSize screenSize = self.bounds.size;
    
    // Đọc ViewMatrix liên tục
    ReadMem(g_ViewMatrixAddr, &g_ViewMatrix, sizeof(Matrix4x4));
    
    // Quét Entity (giả lập vòng lặp, mày cần thay offset chuẩn)
    for (int i = 0; i < 50; i++) {
        uint64_t entityBase = 0;
        if (!ReadMem(g_EntityListAddr + (i * 0x8), &entityBase, 8) || !entityBase) continue;
        
        Vector3 headPos, rootPos;
        // ĐỌC TỌA ĐỘ TỪ BỘ NHỚ (Mày phải thay offset thật vào đây)
        if (!ReadMem(entityBase + 0x1A0, &headPos, sizeof(Vector3))) continue;
        if (!ReadMem(entityBase + 0x1B0, &rootPos, sizeof(Vector3))) continue;
        
        Vector3 sHead, sRoot;
        if (WorldToScreen(headPos, &sHead, screenSize) && WorldToScreen(rootPos, &sRoot, screenSize)) {
            // Vẽ khung xương (ESP)
            float h = sRoot.y - sHead.y;
            float w = h / 2.0f;
            
            CGContextSetRGBStrokeColor(ctx, 1.0, 0.0, 0.0, 1.0); // Đỏ
            CGContextSetLineWidth(ctx, 2.0);
            CGContextStrokeRect(ctx, CGRectMake(sHead.x - w/2, sHead.y, w, h));
            
            // Aimbot logic (kéo góc nhìn)
            if (self.aimbotEnabled) {
                // Logic bẻ góc camera (Cần hook hàm Update của game)
            }
        }
    }
}
@end

// --- HOOK VÀO GAME ---

__attribute__((constructor))
static void initCheat() {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        
        // 1. Đọc offset từ file offsets.json (do cái tool lúc nãy dump ra)
        NSString *docPath = [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
        NSString *jsonPath = [docPath stringByAppendingPathComponent:@"offsets.json"];
        NSData *data = [NSData dataWithContentsOfFile:jsonPath];
        
        if (data) {
            NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            // Lấy địa chỉ gốc + offset
            uint64_t base = strtoull([json[@"image_base"] UTF8String], NULL, 16);
            
            // Tự động gán địa chỉ (Cần mày thay offset chuẩn vào đây)
            g_ViewMatrixAddr = base + 0x3B2F8C; // Ví dụ
            g_EntityListAddr = base + 0xB8C100; // Ví dụ
        }
        
        // 2. Tạo Overlay UI đè lên game
        UIWindow *window = [UIApplication sharedApplication].keyWindow;
        ESPOverlay *overlay = [[ESPOverlay alloc] initWithFrame:window.bounds];
        [window addSubview:overlay];
    });
}
