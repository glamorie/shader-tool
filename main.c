#include <stdarg.h>
#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include "builtin/builtin.h"

#define Str_(x) #x
#define Str(...) Str_(__VA_ARGS__)

#if defined(ShaderToolRenderThread)
#undef ShaderToolRenderThread
#define ShaderToolRenderThread 1
#else
#define ShaderToolRenderThread 0
#endif

#if !defined(ShaderToolEntry)
#define ShaderToolEntry main
#endif

#if !defined(ShaderToolPsMain)
#define ShaderToolPsMain PsMain
#endif

#define ShaderToolEntryName() S(Str(ShaderToolEntry))
#define ShaderToolPsMainName() S(Str(ShaderToolPsMain))

void
FatalF(const char* Format, ...)
{
  va_list Args;
  va_start(Args, Format);
  temp Temp = TempBegin(ArenaGetScratch(0, 0));
  MessageBoxW(
    NULL, StringToW(StringFv(Format, Args, Temp.Arena), Temp.Arena).Value,
    L"Fatal!", MB_ICONERROR | MB_OK
  );
  TempEnd(Temp);
  abort();
};

typedef struct float4 float4;
struct float4
{
  float x, y, z, w;
};

#define F4(x, y, z, w) ((float4){(x), (y), (z), (w)})

typedef struct float3 float3;
struct float3
{
  float x, y, z;
};

#define F3(x, y, z) ((float3){(x), (y), (z)})

typedef struct float2 float2;
struct float2
{
  float x, y;
};

#define F2(x, y) ((float2){(x), (y)})

typedef struct shader_constants shader_constants;
struct alignas(16) shader_constants
{
  float4 iMouse;
  float2 iResolution;
  float2 iScreen;
  float2 iWheel;
  float2 iTime;
};


#if !defined(ShaderToolVsMain)
#define ShaderToolVsMain __VsMain
#endif

#if !defined(ShaderToolPsMain)
#define ShaderToolPsMain __PsMain
#endif 

#if !defined(ShaderToolEntryPoint)
#define ShaderToolEntryPoint main
#endif

#define ShaderTypes \
"cbuffer shader_constants: register(b0)\n" \
"{\n" \
"  float4 iMouse;\n" \
"  float2 iResolution;\n" \
"  float2 iScreen;\n" \
"  float2 iWheel;\n" \
"  float2 iTime;\n" \
"};\n"

#define VertexShaderSource() \
ShaderTypes \
"float4 VsMain(uint id: SV_VertexID) : SV_POSITION \n" \
"{ \n" \
"  float2 verts[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) }; \n" \
"  return float4(verts[id], 0, 1); \n" \
"} \n"


#define PixelShaderPrefix() \
ShaderTypes \
"float4 " Str(ShaderToolEntry) "(float2 pos);\n" \
"float4 " Str(ShaderToolPsMain) "(float4 pos : SV_POSITION) : SV_TARGET\n" \
"{ \n" \
"  float2 p = pos.xy / iResolution;\n" \
"  return " Str(ShaderToolEntryPoint) "(float2(p.x, p.y) * iResolution); \n" \
"} \n" \
"#line 0 \n"

#define PixelShaderSource() \
PixelShaderPrefix() \
"float4 "Str(ShaderToolEntry)"(float2 pos) \n" \
"{ \n" \
"  float2 uv = pos / iResolution.xy; \n" \
"  return float4(uv.x, 0.0, uv.y, 1.0); \n" \
"} \n"


typedef struct paint paint;
struct paint
{
  HWND Hwnd;
  ID3D11Device* Device;
  ID3D11Device1* Device1;
  ID3D11DeviceContext* Context;
  ID3D11DeviceContext1* Context1;
  ID3D11RenderTargetView* Target;
  IDXGISwapChain1* SwapChain;
  // shader
  ID3D11VertexShader* VsMain;
  ID3D11PixelShader* PsMain;
  ID3D11PixelShader* PsUser;
  ID3D11Buffer* PsBuffer;
  int Width, Height;
};

#define ComRelease(Ptr) \
do { if (Ptr) (Ptr)->lpVtbl->Release(Ptr); Ptr = NULL; } while(0);

#define BlobPtr(Ptr) \
((Ptr)? (Ptr)->lpVtbl->GetBufferPointer(Ptr) : 0)

#define BlobSize(Ptr) \
((Ptr)? (Ptr)->lpVtbl->GetBufferSize(Ptr) : 0)

#define CheckHr(Hr) \
if (FAILED(Hr)) goto CleanupHr;

paint*
PaintMake(HWND Hwnd, arena* Arena)
{
  paint* Paint = ArenaZPush(Arena, sizeof(*Paint), alignof(paint));
  
  if (Paint)
  {
    D3D_FEATURE_LEVEL FeatureLevelChoice = 0;
    
    D3D_FEATURE_LEVEL FeatureLevels[] = 
    {
      D3D_FEATURE_LEVEL_12_2,
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
    };
    
    UINT FeatureCount = ArrayLen(FeatureLevels);
    
    D3D_DRIVER_TYPE Drivers[] =
    {
      D3D_DRIVER_TYPE_HARDWARE,
    };
    
    UINT DriverCount = ArrayLen(Drivers);
    UINT Flags = D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    
    ID3D11Device* Device = NULL;
    ID3D11Device1* Device1 = NULL;
    ID3D11DeviceContext* Context = NULL;
    ID3D11DeviceContext1* Context1 = NULL;
    IDXGIAdapter* Adapter = NULL;
    IDXGIFactory2* Factory2 = NULL;
    ID3D11RenderTargetView* Target = NULL;
    IDXGISwapChain1* SwapChain = NULL;
    ID3D11Texture2D* BackBuffer = NULL;
    ID3D11VertexShader* VsMain = NULL;
    ID3D11PixelShader* PsMain = NULL;
    ID3D11PixelShader* PsUser = NULL;
    ID3D11Buffer* PsBuffer = NULL;
    
    ID3DBlob* PsBlob = NULL;
    ID3DBlob* VsBlob = NULL;
    ID3DBlob* ErrorBlob = NULL;
    IDXGIDevice* DeviceX = 0;
    int Width = 0, Height = 0;
    HRESULT Result = ERROR_SUCCESS;
    
    
    for (usize i = 0; i < DriverCount; i++)
    {
      Result = D3D11CreateDevice(
        NULL, Drivers[i], NULL, Flags, FeatureLevels, FeatureCount,
        D3D11_SDK_VERSION, &Device, &FeatureLevelChoice, &Context
      );
      
      if (SUCCEEDED(Result)) break;
    };
    CheckHr(Result);
    Result = Device->lpVtbl->QueryInterface(
      Device, &IID_IDXGIDevice,
      (void**)&DeviceX
    );
    
    Result = DeviceX->lpVtbl->GetAdapter(
      DeviceX, &Adapter
    );
    CheckHr(Result);
    
    // Get the factory
    Result = Adapter->lpVtbl->GetParent(
      Adapter, &IID_IDXGIFactory2,
      (void**)&Factory2
    );
    CheckHr(Result);
    // Query Device1
    Result = Device->lpVtbl->QueryInterface(
      Device, &IID_ID3D11Device1,
      (void**)&Device1
    );
    CheckHr(Result);
    
    
    Result = Context->lpVtbl->QueryInterface(
      Context, &IID_ID3D11DeviceContext1, 
      (void**)&Context1
    );
    
    CheckHr(Result);
    
    RECT Client = {0};
    GetClientRect(Hwnd, &Client);
    
    DXGI_SWAP_CHAIN_DESC1 Desc = {0};
    Desc.Width = Width = Client.right - Client.left;
    Desc.Height = Height = Client.bottom - Client.top;
    
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // BGRA for DComp
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    
    
    Result = Factory2->lpVtbl->CreateSwapChainForHwnd(
      Factory2, (IUnknown*)Device, Hwnd, 
      &Desc, NULL, NULL, &SwapChain
    );
    CheckHr(Result);
    
    Result = SwapChain->lpVtbl->GetBuffer(
      SwapChain, 0, &IID_ID3D11Texture2D,
      (void**)&BackBuffer
    );
    CheckHr(Result);
    
    Result = Device1->lpVtbl->CreateRenderTargetView(
      Device1, (ID3D11Resource*)BackBuffer, NULL,
      &Target
    );

    CheckHr(Result);
    
    Context1->lpVtbl->OMSetRenderTargets(
      Context1, 1, &Target, NULL
    );

    CheckHr(Result);
    
    string Vs = S(VertexShaderSource());
    Result = D3DCompile(
      Vs.Value, Vs.Length, "Default.hlsl", 
      NULL, NULL, "VsMain",
      "vs_5_0", 0, 0, 
      &VsBlob, &ErrorBlob
    );
    CheckHr(Result);
    
    Result = Device1->lpVtbl->CreateVertexShader(
      Device1, VsBlob->lpVtbl->GetBufferPointer(VsBlob),
      VsBlob->lpVtbl->GetBufferSize(VsBlob),
      NULL, &VsMain
    );
    CheckHr(Result);
    
    string Ps = S(PixelShaderSource());
    Result = D3DCompile(
      Ps.Value, Ps.Length, "Default.hlsl",
      NULL, NULL, Str(ShaderToolPsMain),
      "ps_5_0", 0, 0,
      &PsBlob, &ErrorBlob
    );
    
    CheckHr(Result);
    
    Result = Device1->lpVtbl->CreatePixelShader(
      Device1, PsBlob->lpVtbl->GetBufferPointer(PsBlob),
      PsBlob->lpVtbl->GetBufferSize(PsBlob), NULL,
      &PsMain
    );
    CheckHr(Result);
    
    D3D11_BUFFER_DESC BufferDesc = {0};
    BufferDesc.ByteWidth = sizeof(shader_constants);
    BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    BufferDesc.MiscFlags = 0;
    BufferDesc.StructureByteStride = 0;
    Result = Device1->lpVtbl->CreateBuffer(
      Device1, &BufferDesc, NULL, &PsBuffer
    );
    CheckHr(Result);
    
    CleanupHr:
    
    ComRelease(PsBlob);
    ComRelease(VsBlob);
    ComRelease(ErrorBlob);
    ComRelease(BackBuffer);
    ComRelease(DeviceX);
    ComRelease(Factory2);
    
    
    if (FAILED(Result))
    {
      ComRelease(Device);
      ComRelease(Device1);
      ComRelease(Context);
      ComRelease(Context1);
      ComRelease(Adapter);
      ComRelease(Target);
      ComRelease(SwapChain);
      ComRelease(VsMain);
      ComRelease(PsMain);
      ComRelease(PsUser);
      ComRelease(PsBuffer);
      ArenaPop(Arena, sizeof(paint));
      FatalF("Could not initialize direct x\n");
    } else 
    {
      Paint->Hwnd = Hwnd;
      Paint->Device = Device;
      Paint->Device1 = Device1;
      Paint->Context = Context;
      Paint->Context1 = Context1;
      Paint->Target = Target;
      Paint->SwapChain = SwapChain;
      Paint->VsMain = VsMain;
      Paint->PsMain = PsMain;
      Paint->PsUser = PsUser;
      Paint->PsBuffer = PsBuffer;
      Paint->Width = Width;
      Paint->Height = Height;
    };
  };
  return Paint;
};

ID3D11PixelShader*
PaintCompileUserShader(paint* Paint, string FileName, string Source, string* Error, arena* Arena)
{
  ID3D11PixelShader* Out = NULL;
  
  if (Paint)
  {
    ID3DBlob* PsBlob = NULL;
    ID3DBlob* ErrorBlob = NULL;
    HRESULT Result = E_FAIL;
    UINT Flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    TempScope(Arena)
    {
      string FullShader = StringJoinF(Arena, S(""), S(PixelShaderPrefix()), Source);
      
      Result = D3DCompile(
        FullShader.Value, FullShader.Length, (char*)FileName.Value, NULL, NULL,
        Str(ShaderToolPsMain), "ps_5_0",
        Flags, 0, &PsBlob, &ErrorBlob
      );
    };
    
    if (FAILED(Result))
    {
      *Error = StringF(Arena, "%.*s\n", (int)BlobSize(ErrorBlob), BlobPtr(ErrorBlob));
    } else
    {
      Result = Paint->Device1->lpVtbl->CreatePixelShader(
        Paint->Device1, BlobPtr(PsBlob), BlobSize(PsBlob),
        NULL, &Out
      );
    };
    
    ComRelease(PsBlob);
    ComRelease(ErrorBlob);
  };
  return Out;
};

void
PaintTake(paint* Paint)
{
  if (!Paint) return;
  
  ComRelease(Paint->Device);
  ComRelease(Paint->Device1);
  ComRelease(Paint->Context);
  ComRelease(Paint->Context1);
  ComRelease(Paint->Target);
  ComRelease(Paint->SwapChain);
  ComRelease(Paint->VsMain);
  ComRelease(Paint->PsMain);
  ComRelease(Paint->PsUser);
  ComRelease(Paint->PsBuffer);
  MemoryZero(Paint, sizeof(paint));
};

void
PaintResize(paint* Paint, int Width, int Height)
{
  Width = MaxI(0, Width);
  Height = MaxI(0, Height);
  
  if (!Paint || Paint->Width == Width && Paint->Height == Height) return;
  
  ID3D11RenderTargetView* Target = NULL;
  Paint->Context1->lpVtbl->OMSetRenderTargets(
    Paint->Context1, 1, &Target, 0
  );
  
  ComRelease(Paint->Target);
  
  Paint->SwapChain->lpVtbl->ResizeBuffers(
    Paint->SwapChain, 0, Width, Height,
    DXGI_FORMAT_UNKNOWN, 0
  );
  
  ID3D11Texture2D* BackBuffer = NULL;
  Paint->SwapChain->lpVtbl->GetBuffer(
    Paint->SwapChain, 0, &IID_ID3D11Texture2D,
    (void**)&BackBuffer
  );
  
  Paint->Device1->lpVtbl->CreateRenderTargetView(
    Paint->Device1, (ID3D11Resource*)BackBuffer, NULL,
    &Paint->Target
  );
  
  Paint->Context1->lpVtbl->OMSetRenderTargets(
    Paint->Context1, 1, &Paint->Target, NULL
  );
  
  ComRelease(BackBuffer);
  
  Paint->Width = Width;
  Paint->Height = Height;
};

void
PaintRender(paint* Paint, float4 Background, shader_constants* Constants)
{
  if (!Paint) return;
  
  FLOAT Clear[4] = {Background.x, Background.y, Background.z, Background.w};
  Paint->Context1->lpVtbl->OMSetRenderTargets(
    Paint->Context1, 1, &Paint->Target, NULL
  );
  Paint->Context1->lpVtbl->VSSetConstantBuffers(Paint->Context1, 0, 0, NULL);
  Paint->Context1->lpVtbl->PSSetConstantBuffers(Paint->Context1, 0, 0, NULL);
  
  D3D11_VIEWPORT ViewPort = {0};
  ViewPort.TopLeftX = 0;
  ViewPort.TopLeftY = 0;
  ViewPort.Width = (FLOAT)Paint->Width;
  ViewPort.Height = (FLOAT)Paint->Height;
  ViewPort.MinDepth = 0;
  ViewPort.MaxDepth = 1;
  
  Paint->Context1->lpVtbl->RSSetViewports(
    Paint->Context1, 1, &ViewPort
  );
  
  Paint->Context1->lpVtbl->IASetInputLayout(
    Paint->Context1, NULL
  );
  
  Paint->Context1->lpVtbl->IASetPrimitiveTopology(
    Paint->Context1, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
  );
  
  ID3D11Buffer* VertexBuffer[] = {NULL};
  UINT Stride = 0;
  UINT Offset = 0;
  
  Paint->Context1->lpVtbl->IASetVertexBuffers(
    Paint->Context1, 0, 1, VertexBuffer, 
    &Stride, &Offset
  );
  
  
  D3D11_MAPPED_SUBRESOURCE Mapped = {0};
  
  HRESULT Result = Paint->Context1->lpVtbl->Map(
    Paint->Context1, (ID3D11Resource*)Paint->PsBuffer, 0,
    D3D11_MAP_WRITE_DISCARD, 0, &Mapped
  );
  
  if (SUCCEEDED(Result))
  {
    MemoryCopy(Mapped.pData, Constants, sizeof(*Constants));
    Paint->Context1->lpVtbl->Unmap(
      Paint->Context1, (ID3D11Resource*)Paint->PsBuffer,
      0
    );
  };
  
  Paint->Context1->lpVtbl->VSSetShader(
    Paint->Context1, Paint->VsMain,
    NULL, 0
  );
  
  Paint->Context1->lpVtbl->PSSetShader(
    Paint->Context1, Or(Paint->PsUser, Paint->PsMain),
    NULL, 0
  );
  
  Paint->Context1->lpVtbl->VSSetConstantBuffers(
    Paint->Context1, 0, 1,
    &Paint->PsBuffer
  );
  
  Paint->Context1->lpVtbl->PSSetConstantBuffers(
    Paint->Context1, 0, 1,
    &Paint->PsBuffer
  );
  
  Paint->Context1->lpVtbl->ClearRenderTargetView(
    Paint->Context1, Paint->Target,
    Clear
  );
  
  Paint->Context1->lpVtbl->Draw(
    Paint->Context1, 3, 0
  );
  
  Result = Paint->SwapChain->lpVtbl->Present(
    Paint->SwapChain, 1, 0
  );
};

// For some reason SetFullScreenState crashes the gpu. TODO: implement full screening
void
PaintEnterFullScreen(paint* Paint)
{
  if (!Paint) return;
};

void
PaintLeaveFullScreen(paint* Paint)
{
  if (!Paint) return;
};

typedef struct timer timer;
struct timer
{
  LARGE_INTEGER Frequency;
  LARGE_INTEGER Counter;
};

void
TimerInit(timer* Timer)
{
  QueryPerformanceFrequency(&Timer->Frequency);
  QueryPerformanceCounter(&Timer->Counter);
};

double
TimerGet(timer* Timer)
{
  LARGE_INTEGER Counter;
  QueryPerformanceCounter(&Counter);
  return (double)(Counter.QuadPart) / (double)Timer->Frequency.QuadPart;
};

double
TimerUpdate(timer* Timer)
{
  LARGE_INTEGER Counter;
  QueryPerformanceCounter(&Counter);
  double dt = (double)(Counter.QuadPart - Timer->Counter.QuadPart) / (double)Timer->Frequency.QuadPart;
  Timer->Counter = Counter;
  return dt;
};

typedef struct fullscreen fullscreen;
struct fullscreen
{
  RECT Rect;
  LONG WindowStyle;
  LONG WindowStyleEx;
  LONG On;
  int Zoomed;
};
void
FullScreenEnter(fullscreen* FullScreen, HWND Hwnd)
{
  if (!FullScreen || FullScreen->On) return;
  
  FullScreen->On = 1;
  FullScreen->Zoomed = IsZoomed(Hwnd);
  FullScreen->WindowStyle = GetWindowLongW(Hwnd, GWL_STYLE);
  FullScreen->WindowStyleEx = GetWindowLongW(Hwnd, GWL_EXSTYLE);
  GetWindowRect(Hwnd, &FullScreen->Rect);
  
  SetWindowLongW(Hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
  SetWindowLongW(Hwnd, GWL_EXSTYLE, FullScreen->WindowStyleEx); // keep it
  
  HMONITOR Monitor = MonitorFromWindow(Hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO MonitorInfo = {0};
  MonitorInfo.cbSize = sizeof(MonitorInfo);
  GetMonitorInfoW(Monitor, &MonitorInfo);
  
  SetWindowPos(
    Hwnd, NULL,
    MonitorInfo.rcMonitor.left, MonitorInfo.rcMonitor.top,
    MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
    MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
    SWP_FRAMECHANGED | SWP_NOZORDER
  );
};

void
FullScreenLeave(fullscreen* FullScreen, HWND Hwnd)
{
  if (!FullScreen || !FullScreen->On) return;
  
  FullScreen->On = 0;
  
  // Restore styles FIRST
  SetWindowLongW(Hwnd, GWL_STYLE, FullScreen->WindowStyle);
  SetWindowLongW(Hwnd, GWL_EXSTYLE, FullScreen->WindowStyleEx);
  
  SetWindowPos(
    Hwnd, NULL,
    FullScreen->Rect.left, FullScreen->Rect.top,
    FullScreen->Rect.right - FullScreen->Rect.left, 
    FullScreen->Rect.bottom - FullScreen->Rect.top,
    SWP_FRAMECHANGED | SWP_NOZORDER
  );
  
  if (FullScreen->Zoomed)
  ShowWindow(Hwnd, SW_MAXIMIZE);
  else
  ShowWindow(Hwnd, SW_NORMAL);
};

void
FullScreenToggle(fullscreen* FullScreen, HWND Hwnd)
{
  if (FullScreen->On)
  {
    FullScreenLeave(FullScreen, Hwnd);
  } else
  {
    FullScreenEnter(FullScreen, Hwnd);
  };
};

#if ShaderToolThread
typedef struct paint_thread paint_thread;
struct paint_thread
{
  CRITICAL_SECTION CriticalSection;
  paint* Paint;
  shader_constants Constants;
  HANDLE Thread;
  volatile int Width, Height;
  volatile u8 ShouldResize;
  volatile u8 ShouldClose;
};

DWORD CALLBACK
PaintThreadProc(void* Ptr)
{
  paint_thread* PThread = Ptr;
  timer Timer = {0};
  TimerInit(&Timer);
  float Start = TimerGet(&Timer);
  int Width = 0, Height = 0;
  const double FPS = 60.0;
  
  while (1)
  {
    EnterCriticalSection(&PThread->CriticalSection);
    u8 ShouldClose = PThread->ShouldClose;
    u8 ShouldResize = PThread->ShouldResize;
    Width = PThread->Width;
    Height = PThread->Height;
    shader_constants Constants = PThread->Constants;
    PThread->ShouldClose = 0;
    PThread->ShouldResize = 0;
    LeaveCriticalSection(&PThread->CriticalSection);
    
    if (ShouldClose) break;
    
    if (ShouldResize)
    {
      PaintResize(PThread->Paint, Width, Height);
    };
    Constants.iResolution.x = Width;
    Constants.iResolution.y = Height;
    
    double dt = TimerUpdate(&Timer);
    Constants.iTime.x = TimerGet(&Timer) - Start;
    Constants.iTime.y = dt;
    
    PaintRender(PThread->Paint, F4(0.2, 0.2, 0.2, 1.0), &Constants);
    
    if (!ShouldResize && dt < 1.0 / FPS)
    {
      Sleep((DWORD)((1.0 / FPS - dt )* 1000));
    };
  };
  return 0;
};

paint_thread*
PaintThreadOpen(paint* Paint, arena* Arena)
{
  paint_thread* Thread = ArenaZPush(Arena, sizeof(*Thread), alignof(paint_thread));
  
  if (Thread)
  {
    InitializeCriticalSection(&Thread->CriticalSection);
    Thread->Paint = Paint;
    Thread->Thread = CreateThread(0, 0, PaintThreadProc, Thread, 0, 0);
  };
  
  return Thread;
};

void
PaintThreadClose(paint_thread* Thread)
{
  if (!Thread) return;
  EnterCriticalSection(&Thread->CriticalSection);
  Thread->ShouldClose = 1;
  LeaveCriticalSection(&Thread->CriticalSection);
  WaitForSingleObject(Thread->Thread, INFINITE);
  
  CloseHandle(Thread->Thread);
  DeleteCriticalSection(&Thread->CriticalSection);
  MemoryZero(Thread, sizeof(*Thread));
};

void
PaintThreadResize(paint_thread* Thread, int Width, int Height)
{
  if (!Thread) return;
  EnterCriticalSection(&Thread->CriticalSection);
  Thread->ShouldResize = 1;
  Thread->Width = Width;
  Thread->Height = Height;
  LeaveCriticalSection(&Thread->CriticalSection);
};

void
PaintThreadUpdateConstants(paint_thread* Thread, shader_constants* Constants)
{
  if (!Thread) return;
  EnterCriticalSection(&Thread->CriticalSection);
  Thread->Constants = *Constants;
  LeaveCriticalSection(&Thread->CriticalSection);
};

#endif // FeatureRenderThread

typedef struct app app;
struct app
{
  arena* Arena;
  HWND Hwnd;
  paint* Paint;
  shader_constants Constants;
  timer Timer;
  fullscreen FullScreen;
#if ShaderToolThread
  paint_thread* Thread;
#endif
  int Width, Height;
};

app*
AppMake(HWND Hwnd)
{
  arena* Arena= ArenaMake(1<<20, 10<<10, 0);
  app* App = ArenaZPush(Arena, sizeof(*App), alignof(app));
  
  if (App)
  {
    App->Arena = Arena;
    App->Hwnd = Hwnd;
    App->Paint = PaintMake(Hwnd, Arena);
    TimerInit(&App->Timer);
#if ShaderToolThread
    App->Thread = PaintThreadOpen(App->Paint, Arena);
#endif
  };
  return App;
};

void
AppTake(app* App)
{
  if (!App) return;
#if ShaderToolThread
    PaintThreadClose(App->Thread);
#endif
  PaintTake(App->Paint);
  ArenaTake(App->Arena);
};

void
AppResize(app* App, int Width, int Height)
{
  if (!App) return;
#if ShaderToolThread
  PaintThreadResize(App->Thread, Width, Height);
#else
  App->Constants.iResolution.x = (float)Width;
  App->Constants.iResolution.y = (float)Height;
  PaintResize(App->Paint, Width, Height);
  App->Width = Width;
  App->Height = Height;
#endif
};

void
AppMouseMove(app* App, float x, float y)
{
  if (!App) return;
  
  App->Constants.iMouse.x = x;
  App->Constants.iMouse.y = y;
#if ShaderToolThread
  PaintThreadUpdateConstants(App->Thread, &App->Constants);
#endif
};

void
AppMouseDown(app* App, float x, float y)
{
  if (!App) return;
  App->Constants.iMouse.z = x;
  App->Constants.iMouse.w = App->Height -y;
#if ShaderToolThread
  PaintThreadUpdateConstants(App->Thread, &App->Constants);
#endif
};

void
AppMouseUp(app* App, float x, float y)
{
  if (!App) return;
  App->Constants.iMouse.z = x;
  App->Constants.iMouse.w = App->Height - y;
#if ShaderToolThread
  PaintThreadUpdateConstants(App->Thread, &App->Constants);
#endif
};

void
AppMouseWheel(app* App, float dx, float dy)
{
  if (!App) return;
  App->Constants.iWheel.x += dx;
  App->Constants.iWheel.y -= dy;
#if ShaderToolThread
  PaintThreadUpdateConstants(App->Thread, &App->Constants);
#endif
};

void
AppKeyUp(app* App, u32 Key)
{
  if (!App) return;
  
  switch (Key)
  {
    case VK_ESCAPE:
    {
      if (App->FullScreen.On)
      {
        PaintLeaveFullScreen(App->Paint);
      };
      FullScreenLeave(&App->FullScreen, App->Hwnd);
    } break;
    case 'F':
    {
      if (App->FullScreen.On)
      {
        PaintLeaveFullScreen(App->Paint);
      } else
      {
        PaintEnterFullScreen(App->Paint);
      };
      FullScreenToggle(&App->FullScreen, App->Hwnd);
    } break;
  };
};

void
AppPaint(app* App)
{
  if (!App) return;
#if !(ShaderToolThread)  
  double dt = TimerUpdate(&App->Timer);
  App->Constants.iTime.x += dt;
  App->Constants.iTime.y = dt;
  PaintRender(App->Paint, F4(0.2, 0.2, 0.2, 1.0), &App->Constants);
#endif
};

LRESULT CALLBACK
AppWindowProc(HWND Hwnd, UINT Msg, WPARAM Wparam, LPARAM Lparam)
{
  app* App = (app*)GetWindowLongPtrW(Hwnd, GWLP_USERDATA);
  
  switch (Msg)
  {
    case WM_CREATE:
    {
      App = AppMake(Hwnd);
      SetWindowLongPtrW(Hwnd, GWLP_USERDATA, (LONG_PTR)App);
      BOOL Dark = 1;
      DwmSetWindowAttribute(Hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));
      
    } return 0;
    case WM_DESTROY:
    {
      AppTake(App);
      PostQuitMessage(0);
    } return 0;
    case WM_PAINT:
    {
      PAINTSTRUCT Ps;
      BeginPaint(Hwnd, &Ps);
      AppPaint(App);
      EndPaint(Hwnd, &Ps);
    } return 0;
    case WM_KEYUP:
    {
      AppKeyUp(App, (u32)Wparam);
    } return 0;
    case WM_SIZE:
    {
      AppResize(App, LOWORD(Lparam), HIWORD(Lparam));
    } return 0;
    case WM_MOUSEMOVE:
    {
      AppMouseMove(App, GET_X_LPARAM(Lparam), GET_Y_LPARAM(Lparam));
    } return 0;
    case WM_LBUTTONDOWN:
    {
      AppMouseDown(App, GET_X_LPARAM(Lparam), GET_Y_LPARAM(Lparam));
    } return 0;
    case WM_LBUTTONUP:
    {
      AppMouseUp(App, GET_X_LPARAM(Lparam), GET_Y_LPARAM(Lparam));
    } return 0;
    case WM_MOUSEWHEEL:
    {
      AppMouseWheel(App, 0, GET_WHEEL_DELTA_WPARAM(Lparam));
    } return 0;
    case WM_MOUSEHWHEEL:
    {
      AppMouseWheel(App, GET_WHEEL_DELTA_WPARAM(Lparam), 0);
    } return 0;
  };
  return DefWindowProcW(Hwnd, Msg, Wparam, Lparam);
};

app*
AppWindowMake(const char* Name, int Width, int Height)
{
  temp Temp = TempBegin(ArenaGetScratch(0, 0));

  stringw Namew = StringToW(StringCAs(Name), Temp.Arena);
  
  wchar_t* ClassName = L"ShaderTool.Window";
  HINSTANCE ModuleHandle = GetModuleHandleA(0);
  
  WNDCLASSEXW Wc = {0};
  Wc.cbSize = sizeof(Wc);
  Wc.hInstance = ModuleHandle;
  Wc.lpfnWndProc = AppWindowProc;
  Wc.lpszClassName = ClassName;
  Wc.hCursor = LoadCursorA(0, IDC_ARROW);
  
  RegisterClassExW(&Wc);
  
  HWND Hwnd = CreateWindowExW(
    WS_EX_NOREDIRECTIONBITMAP, ClassName, Namew.Value, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
    Width, Height, 0, 0, ModuleHandle, 
    0
  );

  TempEnd(Temp);
  return (app*) GetWindowLongPtrW(Hwnd, GWLP_USERDATA);
};

void
AppEnableDpiAwareness(void)
{
  typedef BOOL WINAPI set_process_dpi_aware(VOID);
  typedef BOOL set_dpi_awareness_context(DPI_AWARENESS_CONTEXT Value);

  HANDLE User32 = LoadLibraryA("User32.dll");

  set_dpi_awareness_context* SetDpiAwarenessContext = (set_dpi_awareness_context*)GetProcAddress(User32, "SetDpiAwarenessContext");
  set_process_dpi_aware* SetProcessDpiAware = (set_process_dpi_aware*)GetProcAddress(User32, "SetProcessDpiAware");

  if (!SetDpiAwarenessContext || !SetDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
  {
    if (SetProcessDpiAware)
    {
      SetProcessDpiAware();
    };
  };
};

u32
StringIsValidFunctionName(string String)
{
  for (usize i = 0; i < String.Length; i++)
  {
    u32 Char = String.Value[i];
    u32 Ok = (
      CharIsAlnum(Char) ||
      Char == '_'
    );

    if (!Ok) return 0;
  };

  return !!String.Length;
};

int wWinMain(HINSTANCE a, HINSTANCE b, LPWSTR c, int d)
{
  if (!StringIsValidFunctionName(ShaderToolEntryName()))
  {
    FatalF("<%s> is not a valid virtual entry point.", ShaderToolEntryName().Value);
    return 1;
  };

  if (!StringIsValidFunctionName(ShaderToolPsMainName()))
  {
    FatalF("<%s> is not a valid entry point.", ShaderToolPsMainName().Value);
    return 1;
  };

  if (StringEqual(ShaderToolEntryName(), ShaderToolPsMainName()))
  {
    FatalF("Virtual entry point cannot be equal to the actual entry point.");
    return 1;
  };

  AppEnableDpiAwareness();

  app* App = AppWindowMake("Shader Tool", 1200, 600);

  TempScope(App->Arena)
  {
    string Path = S("Shader.hlsl"); // TODO: Maybe get it from the command line

    if (PathExists(Path))
    {
      string Shader = PathReadAll(Path, NULL, App->Arena);  
      string Error = {0};
      App->Paint->PsUser = PaintCompileUserShader(
        App->Paint, Path, Shader, &Error,
        App->Arena
      );    
      if (Error.Length)
      {
        FatalF("Error: \n%s\n", Error.Value);
      };
    };
  };

  ShowWindow(App->Hwnd, SW_SHOWDEFAULT);

#if ShaderToolThread
  MSG Msg ;
  while (GetMessageW(&Msg, 0, 0, 0))
  {
    TranslateMessage(&Msg);
    DispatchMessageW(&Msg);
  }
#else
  const double FPS = 60.0f;
  u32 Running = 2;
  
  timer Frames = {0};
  
  TimerInit(&Frames);
  
  while (Running)
  {
    MSG Msg = {0};
    
    while (PeekMessageW(&Msg, 0, 0, 0, PM_REMOVE))
    {
      if (Msg.message == WM_QUIT)
      {
        Running = 0;
        break;
      };
      TranslateMessage(&Msg);
      DispatchMessageW(&Msg);
    };
    
    if (!Running) break;
    
    float dt = TimerUpdate(&Frames);
    
    AppPaint(App);
    
    if (dt < 1.0 / FPS)
    {
      Sleep((DWORD)(1000.0 * (1.0 /FPS - dt)));
    };
  };
#endif
  return 0;
};

#include "builtin/builtin.c"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib")