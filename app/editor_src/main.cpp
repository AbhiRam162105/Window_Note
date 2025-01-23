#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx10.h"
#include <d3d10_1.h>
#include <d3d10.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <map>
#include <commdlg.h>
#include <fstream>
#include <sstream>
#include "md4c.h"
#include "md4c-html.h"

// Data
static ID3D10Device* g_pd3dDevice = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D10RenderTargetView* g_mainRenderTargetView = nullptr;

// Font data
struct FontData {
    ImFont* regular;
    ImFont* bold;
    ImFont* italic;
    ImFont* boldItalic;
} g_Fonts;

// HTML parsing helper struct
struct HTMLTag {
    std::string tag;
    bool isClosing;
    std::map<std::string, std::string> attributes;
};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::string OpenFileDialog();
void SaveTextToFile(const std::string& text, const std::string& filename);
void RenderFormattedText(const std::string& html);
HTMLTag ParseHTMLTag(const std::string& tag);
bool InitializeFonts();

// font initialization with more elegant fonts
bool InitializeFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Configure font settings for better rendering
    ImFontConfig config;
    config.OversampleH = 4;
    config.OversampleV = 4;
    config.PixelSnapH = false;
    config.RasterizerMultiply = 1.2f;

    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2018, 0x201F, // Curly quotes
        0x2013, 0x2014, // En and Em dashes
        0,
    };

    // Load regular font
    g_Fonts.regular = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 16.0f, &config, ranges);
    if (!g_Fonts.regular) g_Fonts.regular = io.Fonts->AddFontDefault();

    // Load bold font
    config.MergeMode = true;
    g_Fonts.bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 16.0f, &config, ranges);
    if (!g_Fonts.bold) g_Fonts.bold = g_Fonts.regular;

    // Load italic font
    g_Fonts.italic = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolai.ttf", 16.0f, &config, ranges);
    if (!g_Fonts.italic) g_Fonts.italic = g_Fonts.regular;

    // Build font atlas
    io.Fonts->Build();
    return true;
}
// Main code
int main(int, char**)
{
    // Create application window
   //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Snap_Note", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Initialize fonts
    if (!InitializeFonts())
    {
        // Handle font initialization failure
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX10_Init(g_pd3dDevice);

    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    std::vector<char> text_buffer(2048);

    // Main loop
    bool done = false;

    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX10_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Create the main window layout
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open"))
                {
                    std::string file_content = OpenFileDialog();
                    if (!file_content.empty())
                    {
                        text_buffer.assign(file_content.begin(), file_content.end());
                        text_buffer.push_back('\0');
                    }
                }
                if (ImGui::MenuItem("Save"))
                {
                    SaveTextToFile(std::string(text_buffer.data()), "output.txt");
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Set window properties
        ImGui::SetNextWindowPos(ImVec2(0, 20));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

        // Main editor window
        {
            ImGui::Begin("Markdown Editor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            ImVec2 available_size = ImGui::GetContentRegionAvail();
            ImGui::Columns(2, nullptr, true);

            // Editor pane
            ImGui::InputTextMultiline("##source", text_buffer.data(), text_buffer.size(),
                ImVec2(available_size.x * 0.5f, available_size.y),
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_NoHorizontalScroll);

            // Preview pane
            ImGui::NextColumn();
            ImGui::BeginChild("Preview", ImVec2(0, 0), true);

            // Convert markdown to HTML and render
            std::string markdown_text = text_buffer.data();
            std::string html_output;
            md_html(markdown_text.c_str(), markdown_text.size(),
                [](const MD_CHAR* text, MD_SIZE size, void* userdata) {
                    std::string* output = static_cast<std::string*>(userdata);
                    output->append(text, size);
                },
                &html_output, MD_DIALECT_GITHUB, MD_HTML_FLAG_DEBUG);

            RenderFormattedText(html_output);

            ImGui::EndChild();
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDevice->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDevice->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    ImGui_ImplDX10_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
    HRESULT res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D10Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Function to open a file dialog and read the content of the selected file (returns std::string)
std::string OpenFileDialog()
{
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = "All\0*.*\0Text\0*.TXT\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE)
    {
        std::ifstream file(ofn.lpstrFile, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    return "";
}

// Function to save text to a file
void SaveTextToFile(const std::string& text, const std::string& filename)
{
    std::ofstream file(filename);
    file << text;
}


void RenderFormattedText(const std::string& html) {
    // Set up preview styling with dark background and white text
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));

    // Improved padding and spacing
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6.0f, 6.0f));

    std::string::size_type pos = 0;
    std::string::size_type last_pos = 0;
    std::vector<HTMLTag> tag_stack;
    bool in_ordered_list = false;
    bool in_code_block = false;
    int list_number = 1;

    // Fixed heading sizes (instead of scaling)
    const float heading_sizes[] = {
        42.0f,  // h1
        32.0f,  // h2
        24.0f,  // h3
        20.0f,  // h4
        18.0f,  // h5
        16.0f   // h6
    };

    while ((pos = html.find('<', last_pos)) != std::string::npos) {
        if (pos > last_pos) {
            std::string text = html.substr(last_pos, pos - last_pos);
            if (!text.empty() && text != "\n") {
                ImVec4 text_color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
                bool is_bold = false;
                bool is_italic = false;
                bool is_heading = false;
                int heading_level = 0;

                // Process tag stack for styling
                for (const auto& tag : tag_stack) {
                    if (tag.tag == "strong" || tag.tag == "b") is_bold = true;
                    if (tag.tag == "em" || tag.tag == "i") is_italic = true;
                    if (tag.tag[0] == 'h' && tag.tag.length() == 2) {
                        is_heading = true;
                        heading_level = tag.tag[1] - '0';
                        text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    }
                    if (tag.tag == "code") in_code_block = true;
                }

                // Apply appropriate font
                ImFont* current_font = g_Fonts.regular;
                if (is_bold && is_italic) current_font = g_Fonts.boldItalic;
                else if (is_bold) current_font = g_Fonts.bold;
                else if (is_italic) current_font = g_Fonts.italic;

                ImGui::PushFont(current_font);

                // Handle code block with different background color
                if (in_code_block) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    ImGui::BeginChild("CodeBlock", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                // Handle headings with fixed sizes and proper spacing
                else if (is_heading && heading_level > 0 && heading_level <= 6) {
                    // Add spacing before heading
                    ImGui::Dummy(ImVec2(0.0f, heading_level == 1 ? 32.0f : 24.0f));

                    // Use fixed font size instead of scaling
                    float font_size = heading_sizes[heading_level - 1];
                    ImGui::SetWindowFontScale(font_size / 16.0f); // Assuming base font size is 16

                    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();

                    // Reset font scale
                    ImGui::SetWindowFontScale(1.0f);

                    // Add spacing after heading
                    ImGui::Dummy(ImVec2(0.0f, heading_level == 1 ? 24.0f : 16.0f));
                }
                // Handle regular text and lists
                else {
                    if (in_ordered_list) {
                        ImGui::Indent(24.0f);
                        ImGui::Text("%d.", list_number++);
                        ImGui::SameLine(0, 8.0f);
                    }

                    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopTextWrapPos();

                    if (in_ordered_list) {
                        ImGui::Unindent(24.0f);
                    }
                }

                ImGui::PopFont();
            }
        }

        // Process tags
        std::string::size_type end_pos = html.find('>', pos);
        if (end_pos == std::string::npos) break;

        HTMLTag tag = ParseHTMLTag(html.substr(pos + 1, end_pos - pos - 1));

        // Handle ordered lists
        if (tag.tag == "ol") {
            if (!tag.isClosing) {
                in_ordered_list = true;
                list_number = 1;
            }
            else {
                in_ordered_list = false;
            }
        }

        // Handle code blocks
        if (tag.tag == "code") {
            in_code_block = !tag.isClosing;
        }

        // Update tag stack
        if (tag.isClosing) {
            if (!tag_stack.empty() && tag_stack.back().tag == tag.tag) {
                tag_stack.pop_back();
            }
        }
        else {
            tag_stack.push_back(tag);
        }

        last_pos = end_pos + 1;
    }

    // Pop all style modifications
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

HTMLTag ParseHTMLTag(const std::string& tag)
{
    HTMLTag result;
    std::string::size_type pos = 0;

    // Check if it's a closing tag
    if (tag[0] == '/')
    {
        result.isClosing = true;
        pos = 1;
    }
    else
    {
        result.isClosing = false;
    }

    // Get tag name
    std::string::size_type space_pos = tag.find(' ', pos);
    if (space_pos == std::string::npos)
    {
        result.tag = tag.substr(pos);
    }
    else
    {
        result.tag = tag.substr(pos, space_pos - pos);

        // Parse attributes (simplified)
        std::string attrs = tag.substr(space_pos + 1);
        std::string::size_type attr_pos = 0;
        while ((attr_pos = attrs.find('=', attr_pos)) != std::string::npos)
        {
            std::string::size_type name_start = attrs.rfind(' ', attr_pos);
            std::string name = attrs.substr(name_start + 1, attr_pos - name_start - 1);

            std::string::size_type value_start = attrs.find('"', attr_pos);
            std::string::size_type value_end = attrs.find('"', value_start + 1);
            std::string value = attrs.substr(value_start + 1, value_end - value_start - 1);

            result.attributes[name] = value;
            attr_pos = value_end + 1;
        }
    }

    return result;
}


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
