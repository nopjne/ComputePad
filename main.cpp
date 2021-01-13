#include <Windows.h>
#include <assert.h>
#include <string>
#include <vector>


//#if defined(_MSC_PLATFORM_TOOLSET_v140) || defined(_MSC_PLATFORM_TOOLSET_v141) || defined(_MSC_PLATFORM_TOOLSET_v142)
#define D3D11_1
//#endif

#ifdef D3D11_1
#include <d3d11_1.h>
#else
#include <d3d11.h>
#include <d3dx11.h>
#include <d3dx10.h> // Is this needed, and what for ????
#include <DXGI.h>
#endif

#include <d3dcompiler.h>

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "d3dx11.lib")
#pragma comment (lib, "dxgi.lib")

HWND gHwnd;
HINSTANCE gHInstance;
int gMaxFeatureLvel = 11;
int RTWidth = 980;
int RTHeight = 1024;
int DispatchX = RTWidth;
int DispatchY = RTHeight;
int DispatchZ = 1;

#define MULTI_SAMPLE_COUNT 4

LRESULT CALLBACK WindowProcess(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

const char *PixelShaderString = R"(
Texture2D shaderTexture : register (t0);

SamplerState LinearSampling{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};

struct VsOutput {
    float4 Position : SV_POSITION;
    float2 Texture : TEXCOORD0;
};

float4 Entry(VsOutput Input) : SV_TARGET
{
    float4 textureColor;

    textureColor = shaderTexture.Sample(LinearSampling, Input.Texture);
    return textureColor;
}
)";

const char *VertexShaderString = R"(
struct VsOutput {
    float4 Position : SV_POSITION;
    float2 Texture : TEXCOORD0;
};

VsOutput Entry(uint VertexID : SV_VertexID)
{
    VsOutput Out;
    Out.Texture = float2((VertexID << 1) & 2, VertexID & 2);
    Out.Position = float4(Out.Texture * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Out;
}

struct VsInput {
    float4 Position : POSITION0;
    float2 Texture : TEXCOORD0;
};

VsOutput Entry1(VsInput Input)
{
    VsOutput Output;

    Input.Position.w = 1.0f;

    //
    // Transform the vertices by the projection and camera.
    //

    Output.Position = Input.Position;
    Output.Texture = Input.Texture;
    return Output;
}
)";

typedef struct _PRIMITIVE_VERTEX {

    //
    // Location
    //

    FLOAT x;
    FLOAT y;
    FLOAT z;

    //
    // Color
    //

    FLOAT u;
    FLOAT v;
} PRIMITIVE_VERTEX, * PPRIMITIVE_VERTEX;

D3D11_INPUT_ELEMENT_DESC ScaledBlitLayoutDesc[] =
{
    { "POSITION",
    0,
    DXGI_FORMAT_R32G32B32_FLOAT,
    0,
    0,
    D3D11_INPUT_PER_VERTEX_DATA,
    0 },

    { "TEXCOORD",
    0,
    DXGI_FORMAT_R32G32_FLOAT,
    0,
    D3D11_APPEND_ALIGNED_ELEMENT,
    D3D11_INPUT_PER_VERTEX_DATA,
    0 },
};

class ComputeScratchPad {
    ID3D11DeviceContext *m_DeviceContext;
    IDXGISwapChain* m_SwapChain;
    ID3D11Device* m_RenderDevice;
    D3D_FEATURE_LEVEL m_CurrentFl;
    ID3D11RenderTargetView* m_RenderTarget;
    HWND m_hWnd;
    D3D11_TEXTURE2D_DESC m_BackBufferDescriptor;
    ID3D11ComputeShader *m_ComputeShader;
    ID3D11VertexShader *m_VertexShader;
    ID3D11PixelShader *m_PixelShader;

    ID3D11UnorderedAccessView *m_UAV;
    ID3D11ShaderResourceView *m_SRV;

    ID3D11Buffer *m_IndexBuffer;
    ID3D11SamplerState *m_SamplerState;
    D3D11_VIEWPORT m_ViewPort;

    ID3D11Buffer *m_TimeConstant;

#ifdef D3D11_1
    ID3DUserDefinedAnnotation* m_Annotation;
#endif

public: 
    ComputeScratchPad();
    bool D3DInitialize();
    void Render();
    bool LoadShader();
};

ComputeScratchPad::ComputeScratchPad():
    m_PixelShader(nullptr)
{

}

bool ComputeScratchPad::D3DInitialize() 
{
    DWORD CreationFlags = 0;

    D3D_FEATURE_LEVEL FeatureLevel[] = {
#ifdef D3D11_1
        D3D_FEATURE_LEVEL_11_1,
#endif
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        //D3D_FEATURE_LEVEL_9_3,
        //D3D_FEATURE_LEVEL_9_2,
        //D3D_FEATURE_LEVEL_9_1,
    };

    //
    // Setup the desired swapchain.
    //

    DXGI_SWAP_CHAIN_DESC DesiredSwapChain;
    RtlZeroMemory(&DesiredSwapChain, sizeof(DXGI_SWAP_CHAIN_DESC));
    DesiredSwapChain.BufferCount = 1;
    DesiredSwapChain.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DesiredSwapChain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    DesiredSwapChain.OutputWindow = gHwnd;
    DesiredSwapChain.SampleDesc.Count = MULTI_SAMPLE_COUNT;
    DesiredSwapChain.SampleDesc.Quality = 0;
    DesiredSwapChain.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    DesiredSwapChain.Windowed = true;//!m_FullScreen;

#if defined(_DEBUG)
    // If the project is in a debug build, enable the debug layer.
    CreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    //
    // Create the device and its swap buffers.
    //
    HRESULT Status;
    Status = D3D11CreateDeviceAndSwapChain(NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        CreationFlags,
        FeatureLevel,
        ARRAYSIZE(FeatureLevel),
        D3D11_SDK_VERSION,
        &DesiredSwapChain,
        &m_SwapChain,
        &m_RenderDevice,
        &m_CurrentFl,
        &m_DeviceContext);

    assert(Status == S_OK);

    if ((gMaxFeatureLvel == 10) || (D3D_FEATURE_LEVEL_11_0 > m_CurrentFl)) {
        wchar_t String[MAX_PATH];
        GetWindowText(m_hWnd, String, MAX_PATH);
        std::wstring WindowName(String);
        WindowName += L" DX10 - Capabilities limited.";
        SetWindowText(m_hWnd, WindowName.c_str());
    }

    if ((gMaxFeatureLvel == 10) && (m_CurrentFl >= D3D_FEATURE_LEVEL_11_0)) {
        m_CurrentFl = D3D_FEATURE_LEVEL_10_1;
    }

    if (m_CurrentFl < D3D_FEATURE_LEVEL_10_0) {
        MessageBox(m_hWnd, L"Please upgrade drivers, if problem persists upgrade the graphics device.", L"Unsupported Graphics Device.", MB_OK);
        return false;
    }

    HRESULT hr;
#ifdef _DEBUG
    ID3D11Debug* d3dDebug = NULL;
    if (SUCCEEDED(m_RenderDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug)))
    {
        ID3D11InfoQueue* d3dInfoQueue = NULL;
        if (SUCCEEDED(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue)))
        {

            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);
            d3dInfoQueue->Release();
        }

        d3dDebug->Release();
    }

    hr = m_RenderDevice->QueryInterface(__uuidof(m_Annotation), reinterpret_cast<void**>(&m_Annotation));
    if (FAILED(hr)) {
        OutputDebugString(L"Could no initialize the Annotation interface.");
    }
#endif

    //
    // Create the render target and set the output merger to use that for output.
    //
    ID3D11Texture2D* BackBuffer;
    m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&BackBuffer);
    BackBuffer->GetDesc(&m_BackBufferDescriptor);
    if (BackBuffer == nullptr) {
        BackBuffer->Release();
        return false;
    }

    m_RenderDevice->CreateRenderTargetView(BackBuffer, NULL, &m_RenderTarget);
    BackBuffer->Release();

    D3D11_TEXTURE2D_DESC textureDesc;
    ZeroMemory(&textureDesc, sizeof(textureDesc));
    textureDesc.Width = RTWidth;
    textureDesc.Height = RTHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = 0;
    ID3D11Texture2D *Texture = nullptr;
    hr = m_RenderDevice->CreateTexture2D(&textureDesc, 0, &Texture);


    D3D11_UNORDERED_ACCESS_VIEW_DESC descUAV;
    ZeroMemory(&descUAV, sizeof(descUAV));
    descUAV.Format = DXGI_FORMAT_UNKNOWN;
    descUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    descUAV.Texture2D.MipSlice = 0;
    hr = m_RenderDevice->CreateUnorderedAccessView(Texture, &descUAV, &m_UAV);

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_RenderDevice->CreateShaderResourceView(Texture, &srvDesc, &m_SRV);

    m_DeviceContext->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    int Indices[] = { 0, 1, 2, 4 };
    D3D11_BUFFER_DESC VertexBuffer;
    ZeroMemory(&VertexBuffer, sizeof(VertexBuffer));
    VertexBuffer.Usage = D3D11_USAGE_IMMUTABLE;
    VertexBuffer.ByteWidth = sizeof(Indices);
    VertexBuffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
    VertexBuffer.CPUAccessFlags = 0;
    VertexBuffer.StructureByteStride = sizeof(int);
    
    D3D11_SUBRESOURCE_DATA SubresourceData;
    SubresourceData.pSysMem = Indices;
    SubresourceData.SysMemPitch = sizeof(Indices);
    m_RenderDevice->CreateBuffer(&VertexBuffer, &SubresourceData, &m_IndexBuffer);
    
    D3D11_SAMPLER_DESC SamplerDescriptor;
    SamplerDescriptor.Filter = D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    SamplerDescriptor.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDescriptor.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDescriptor.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    SamplerDescriptor.MipLODBias = 0.0f;
    SamplerDescriptor.MaxAnisotropy = 1;
    SamplerDescriptor.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    SamplerDescriptor.BorderColor[0] = 0;
    SamplerDescriptor.BorderColor[1] = 0;
    SamplerDescriptor.BorderColor[2] = 0;
    SamplerDescriptor.BorderColor[3] = 0;
    SamplerDescriptor.MinLOD = 0;
    SamplerDescriptor.MaxLOD = D3D11_FLOAT32_MAX;
    Status = m_RenderDevice->CreateSamplerState(&SamplerDescriptor, &m_SamplerState);

    // Create the constant buffer.

    //
    // Setup matrix buffers.
    //
    D3D11_BUFFER_DESC MatrixBufferDesc;
    ZeroMemory(&MatrixBufferDesc, sizeof(MatrixBufferDesc));
    MatrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    MatrixBufferDesc.ByteWidth = sizeof(float) * 4;
    MatrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    MatrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    MatrixBufferDesc.MiscFlags = 0;
    MatrixBufferDesc.StructureByteStride = 0;

    //
    // Create the constant buffer pointer so the vertex shader's constant buffer
    // is accesible from within this class.
    //

    Status = m_RenderDevice->CreateBuffer(&MatrixBufferDesc, NULL, &m_TimeConstant);
    assert(Status == S_OK);
    return true;
}

#define PRINT_ERROR(Status) \
if (Status != 0) { \
    const char* Error = (char*)(ErrorMessages->GetBufferPointer()); \
    OutputDebugStringA(Error); \
    OutputDebugStringA("\n"); \
    MessageBoxA(gHwnd, Error, "Shader compilation error", MB_OK); \
}

bool ComputeScratchPad::LoadShader()
{
    if (m_CurrentFl >= D3D_FEATURE_LEVEL_11_0) {
        std::vector<byte> ComputeShader;
        
        HRESULT Status;

        // Load the file.
        FILE *FileHandle;
        errno_t ErrorFile;
        ID3DBlob* CompiledComputeBlob;
        ID3DBlob* ErrorMessages;

        do {
            do {
                ErrorFile = fopen_s(&FileHandle, "ComputeShader.hlsl", "rb");
                if (ErrorFile == 0) {
                    fseek(FileHandle, 0, SEEK_END);
                    ComputeShader.resize(ftell(FileHandle));
                    fseek(FileHandle, 0, SEEK_SET);
                    fread(&ComputeShader[0], 1, ComputeShader.size(), FileHandle);
                    fclose(FileHandle);
                }

            } while (ErrorFile != 0);

            // Compile 
            Status =  D3DCompile(
                        &ComputeShader[0],
                        ComputeShader.size(),
                        "ComputeShader.hlsl", // LPCWSTR                pFileName,
                        nullptr,              // const D3D_SHADER_MACRO * pDefines,
                        nullptr,              // ID3DInclude * pInclude,
                        "Entry",              // LPCSTR                 pEntrypoint,
                        "cs_5_0",             // LPCSTR                 pTarget,
                        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,                    // UINT                   Flags1,
                        0,                    // UINT                   Flags2,
                        &CompiledComputeBlob, // ID3DBlob * *ppCode,
                        &ErrorMessages        // ID3DBlob * *ppErrorMsgs
                        );

            PRINT_ERROR(Status);

        } while (Status != S_OK);

        Status = m_RenderDevice->CreateComputeShader(CompiledComputeBlob->GetBufferPointer(),
                                                        CompiledComputeBlob->GetBufferSize(),
                                                        NULL,
                                                        &m_ComputeShader);

        if (m_PixelShader == nullptr) {
            ID3DBlob *CompiledShaderBlob;
            Status = D3DCompile(
                PixelShaderString,    // LPCVOID                pSrcData,
                strlen(PixelShaderString), // SIZE_T                 SrcDataSize,
                "PixelSHDR",               // LPCSTR                 pSourceName,
                nullptr,              // const D3D_SHADER_MACRO * pDefines,
                nullptr,              // ID3DInclude * pInclude,
                "Entry",              // LPCSTR                 pEntrypoint,
                "ps_4_0",             // LPCSTR                 pTarget,
                0,                    // UINT                   Flags1,
                0,                    // UINT                   Flags2,
                &CompiledShaderBlob,  // ID3DBlob * *ppCode,
                &ErrorMessages        // ID3DBlob * *ppErrorMsgs
                );

            PRINT_ERROR(Status);

            Status = m_RenderDevice->CreatePixelShader(CompiledShaderBlob->GetBufferPointer(),
                                                       CompiledShaderBlob->GetBufferSize(),
                                                       NULL,
                                                       &m_PixelShader);

            Status = D3DCompile(
                VertexShaderString,   // LPCVOID                pSrcData,
                strlen(VertexShaderString), // SIZE_T                 SrcDataSize,
                "VertexSHDR",               // LPCSTR                 pSourceName,
                nullptr,              // const D3D_SHADER_MACRO * pDefines,
                nullptr,              // ID3DInclude * pInclude,
                "Entry",              // LPCSTR                 pEntrypoint,
                "vs_4_0",             // LPCSTR                 pTarget,
                0,                    // UINT                   Flags1,
                0,                    // UINT                   Flags2,
                &CompiledShaderBlob,  // ID3DBlob * *ppCode,
                &ErrorMessages        // ID3DBlob * *ppErrorMsgs
                );

            PRINT_ERROR(Status);
            Status = m_RenderDevice->CreateVertexShader(CompiledShaderBlob->GetBufferPointer(),
                                                        CompiledShaderBlob->GetBufferSize(),
                                                        NULL,
                                                        &m_VertexShader);
        }
        return true;
    }

    return false;
}

bool InitWindow() 
{
    DWORD ExStyle;
    DWORD Style;
    WNDCLASS WindowClass = {0};
    RECT WindowRect;

    WindowRect.left = 0;
    WindowRect.top = 0;
    WindowRect.right = RTWidth;
    WindowRect.bottom = RTHeight;

    //
    // Create the necessary window class.
    //

    HINSTANCE m_hInstance = GetModuleHandle(NULL);
    WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    WindowClass.lpfnWndProc = (WNDPROC) WindowProcess; //WindowProcessCallback;
    WindowClass.cbClsExtra = 0;
    WindowClass.cbWndExtra = 0;
    WindowClass.hInstance = m_hInstance;
    WindowClass.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    WindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    WindowClass.hbrBackground = NULL;
    WindowClass.lpszClassName = L"D3DWindowClass";
    if (!RegisterClass(&WindowClass)) {
        MessageBox(NULL,
                   L"Failed To Register The Window Class.",
                   L"ERROR",
                   MB_OK|MB_ICONEXCLAMATION);

        return FALSE;
    }

    //
    // Setup the necessary window descriptors.
    //

    ExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    Style = WS_OVERLAPPEDWINDOW;
    HWND m_hWnd = CreateWindowEx(ExStyle,
                            WindowClass.lpszClassName,
                            L"ComputePad",
                            Style |
                            WS_CLIPSIBLINGS |
                            WS_CLIPCHILDREN,
                            0, 0,
                            WindowRect.right-WindowRect.left,
                            WindowRect.bottom-WindowRect.top,
                            NULL,
                            NULL,
                            m_hInstance,
                            NULL
                            );

    assert(m_hWnd != NULL);
    gHwnd = m_hWnd;
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
    SetFocus(m_hWnd);

    return true;
}

POINT initpos;
POINT pos;
HWND capture;
LRESULT CALLBACK WindowProcess(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_KEYDOWN:
    break;
    case WM_WINDOWPOSCHANGED:
    {
        // Handle window resizing.
        //
        // WINDOWPOS* WindowPos = (WINDOWPOS*)lParam;
        // TextRect.left = WindowPos->x;
        // TextRect.top = WindowPos->y;
        // TextRect.right = WindowPos->x + WindowPos->cx;
        // TextRect.bottom = WindowPos->y + WindowPos->cy;
        // 
        // TransformationMatrix[0] = (float)((TextRect.right - TextRect.left) - 60) / (float)BufferBackX;
        // TransformationMatrix[5] = (float)((TextRect.bottom - TextRect.top) - 100) / (float)BufferBackY;
        // TransformationMatrix[10] = 1.0f;
        // TransformationMatrix[15] = 1.0f;
        // TransformationMatrix[12] = 30.0f;
        // TransformationMatrix[13] = 30.0f;
        // 
        // BackBufferMatrix[0] = (float)(BackW) / (float)BufferBackX;
        // BackBufferMatrix[5] = (float)(BackH) / (float)BufferBackY;
        // BackBufferMatrix[10] = 1.0f;
        // BackBufferMatrix[15] = 1.0f;
    }
    break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        GetCursorPos(&initpos);
        ScreenToClient(hWnd, &initpos);
        //HandleMousePress(initpos.x, initpos.y, (DWORD)wParam);
        capture = SetCapture(GetCapture());
        return 0;
    }
    break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
        GetCursorPos(&pos);
        ScreenToClient(hWnd, &pos);
        //HandleMouseRelease(pos.x, pos.y, (DWORD)wParam);
        ReleaseCapture();
        return 0;
    }
    break;

    case WM_MOUSEMOVE:
    {
        GetCursorPos(&pos);
        ScreenToClient(hWnd, &pos);
        //HandleMouseMove(pos.x, pos.y, (DWORD)wParam);
        return 0;
    }
    break;
    case WM_CLOSE:
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool bShaderChanged = false;
void ComputeScratchPad::Render()
{
    // Load and compile the shader.
    if (bShaderChanged != false) {
        LoadShader();
        bShaderChanged = false;
    }

    static int Time = 0;
    Time += 1;

    // Run compute kernel output to UAV.
    D3D11_MAPPED_SUBRESOURCE MappedResource;
    m_DeviceContext->Map(m_TimeConstant,
                                  0,
                                  D3D11_MAP_WRITE_DISCARD,
                                  0,
                                  &MappedResource);
    
    memcpy(MappedResource.pData, &Time, sizeof(float));
    m_DeviceContext->Unmap(m_TimeConstant, 0);
    m_DeviceContext->CSSetConstantBuffers(0, 1, &m_TimeConstant);

    m_DeviceContext->CSSetShader(m_ComputeShader, NULL, 0);
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, &m_UAV, nullptr);
    m_DeviceContext->Dispatch(DispatchX, DispatchY, DispatchZ);

    // Run the raster pipeline to output the UAV values.
    ID3D11UnorderedAccessView *NullUav = nullptr;
    ID3D11ShaderResourceView* NullSrv = nullptr;
    ID3D11DepthStencilView *NullDsv = nullptr;
    m_DeviceContext->PSSetShader(m_PixelShader, NULL, 0);
    m_DeviceContext->VSSetShader(m_VertexShader, NULL, 0);
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, &NullUav, nullptr);
    m_DeviceContext->PSSetShaderResources(0, 1, &m_SRV);
    m_DeviceContext->IASetIndexBuffer(m_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerState);
    m_DeviceContext->OMSetRenderTargets(1, &m_RenderTarget, NullDsv);
    m_ViewPort.Width = (float)RTWidth;
    m_ViewPort.Height = (float)RTHeight;
    m_ViewPort.MaxDepth = 1.0f;
    m_DeviceContext->RSSetViewports(1, &m_ViewPort);
    m_DeviceContext->DrawIndexed(4, 0, 0);

    //  Present the final result.
    int Result = m_SwapChain->Present(1, 0);
    if (Result == DXGI_ERROR_DEVICE_REMOVED || Result == DXGI_ERROR_DEVICE_RESET) {
        // Reinitialize the renderer.
        //return ReInitializeRenderer(Frame);
        MessageBox(gHwnd, L"Re-init required", L"Dead", MB_OK);
        PostQuitMessage(0);
    }

    // Unset the SRV
    m_DeviceContext->PSSetShaderResources(0, 1, &NullSrv);

    //  wait - until button press
    Sleep(20);
}

class FileChangeMonitor {
private:
    HANDLE m_ThreadHandle;
    HANDLE m_WaitHandle;
    HANDLE m_CloseEventHandle;
    DWORD m_ThreadId;
    wchar_t m_FileName[MAX_PATH];
    __time64_t m_ModTime;
public:
    FileChangeMonitor(const wchar_t* FileName);
    virtual ~FileChangeMonitor();
    void CreateListenerThread(void);
    static DWORD WINAPI ThreadedWaiter(LPVOID Parameter);
};

FileChangeMonitor::FileChangeMonitor (
    const wchar_t* FileName)
{
    wcscpy_s(m_FileName, ARRAYSIZE(m_FileName), FileName);
    CreateListenerThread();
}

FileChangeMonitor::~FileChangeMonitor (
    void
    )
{
    SetEvent(m_CloseEventHandle);
    WaitForSingleObject(m_ThreadHandle, INFINITE);
    CloseHandle(m_ThreadHandle);
}

void
FileChangeMonitor::CreateListenerThread(
    void
    )
{
    std::wstring DirectoryPath(m_FileName);
    size_t Location = DirectoryPath.rfind(L"\\");
    DirectoryPath = DirectoryPath.substr(0, Location);

    // Record Current change time
    struct _stat64i32 StatResult;
    if (_wstat(m_FileName, &StatResult) == 0) {
        m_ModTime = StatResult.st_mtime;
    }

    m_CloseEventHandle = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Register new listener.
    m_WaitHandle = FindFirstChangeNotification(
        DirectoryPath.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE
    );

    assert(m_WaitHandle != INVALID_HANDLE_VALUE);

    m_ThreadHandle = CreateThread(NULL, 0, ThreadedWaiter, this, 0, &m_ThreadId);
}

DWORD WINAPI
FileChangeMonitor::ThreadedWaiter(
    LPVOID Parameter
    )
{
    FileChangeMonitor* Self = (FileChangeMonitor*)Parameter;
    HANDLE WaitHandle = Self->m_WaitHandle;
    HANDLE Objects[] = { WaitHandle, Self->m_CloseEventHandle };
    do {
        DWORD WaitCompletion = WaitForMultipleObjects(ARRAYSIZE(Objects), Objects, FALSE, INFINITE);
        if (WaitCompletion == (WAIT_OBJECT_0 + 1)) {
            FindCloseChangeNotification(WaitHandle);
            return 0;
        }

        if (WaitCompletion != WAIT_OBJECT_0) {
            OutputDebugString(L"Unknown file change reason.\n");
        }

        struct _stat64i32 StatResult;
        if (_wstat(Self->m_FileName, &StatResult) == 0) {
            if (Self->m_ModTime != StatResult.st_mtime) {
                Self->m_ModTime = StatResult.st_mtime;

                // Notify engine to reload dirty scripts on next tick.
                bShaderChanged = true;
                InvalidateRect(gHwnd, nullptr, true);
            }
        }
    } while (FindNextChangeNotification(WaitHandle) != 0);

    FindCloseChangeNotification(WaitHandle);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR    lpCmdLine,
    int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);
    gHInstance = hInstance;
    InitWindow();

    // Create UAV.
    ComputeScratchPad Renderer;
    Renderer.D3DInitialize();
    Renderer.LoadShader();

    // Start the shader file listener thread.
    FileChangeMonitor ChangeMonitor(L".\\ComputeShader.hlsl");

    // Main message loop:
    MSG msg;
#if UPDATE_ON_MSG

    while (GetMessage(&msg, NULL, 0, 0))
    {
        Renderer.Render();
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

#else // update on frame.

    bool gRunning = true;
    while (gRunning != FALSE) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                gRunning = FALSE;

            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        Renderer.Render();
    }

#endif

    return 0;
}

