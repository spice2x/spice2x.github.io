// dear imgui: Renderer for DirectX9
// This needs to be used along with a Platform Binding (e.g. Win32)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'LPDIRECT3DTEXTURE9' as ImTextureID. Read the FAQ about ImTextureID in imgui.cpp.
//  [X] Renderer: Support for large meshes (64k+ vertices) with 16-bits indices.

// You can copy and use unmodified imgui_impl_* files in your project. See main.cpp for an example of using this.
// If you are new to dear imgui, read examples/README.txt and read the documentation at the top of imgui.cpp.
// https://github.com/ocornut/imgui

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2019-05-29: DirectX9: Added support for large mesh (64K+ vertices), enable ImGuiBackendFlags_RendererHasVtxOffset flag.
//  2019-04-30: DirectX9: Added support for special ImDrawCallback_ResetRenderState callback to reset render state.
//  2019-03-29: Misc: Fixed erroneous assert in ImGui_ImplDX9_InvalidateDeviceObjects().
//  2019-01-16: Misc: Disabled fog before drawing UI's. Fixes issue #2288.
//  2018-11-30: Misc: Setting up io.BackendRendererName so it can be displayed in the About Window.
//  2018-06-08: Misc: Extracted imgui_impl_dx9.cpp/.h away from the old combined DX9+Win32 example.
//  2018-06-08: DirectX9: Use draw_data->DisplayPos and draw_data->DisplaySize to setup projection matrix and clipping rectangle.
//  2018-05-07: Render: Saving/restoring Transform because they don't seem to be included in the StateBlock. Setting shading mode to Gouraud.
//  2018-02-16: Misc: Obsoleted the io.RenderDrawListsFn callback and exposed ImGui_ImplDX9_RenderDrawData() in the .h file so you can call it yourself.
//  2018-02-06: Misc: Removed call to ImGui::Shutdown() which is not available from 1.60 WIP, user needs to call CreateContext/DestroyContext themselves.

#include "impl_dx9.h"

#include <algorithm>

// DirectX
#include <d3d9.h>

#include "external/imgui/imgui.h"

// allow std::min use
#ifdef min
#undef min
#endif

// DirectX data
static LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
static LPDIRECT3DVERTEXBUFFER9 g_pVB = NULL;
static LPDIRECT3DINDEXBUFFER9 g_pIB = NULL;
static LPDIRECT3DTEXTURE9 g_FontTexture = NULL;
static int g_VertexBufferSize = 5000, g_IndexBufferSize = 10000;

#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1)

// Render function.
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
void ImGui_ImplDX9_RenderDrawData(ImDrawData *draw_data) {

    // Avoid rendering when minimized
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f)
        return;

    // Create and grow buffers if needed
    if (!g_pVB || g_VertexBufferSize < draw_data->TotalVtxCount) {
        if (g_pVB) {
            g_pVB->Release();
            g_pVB = NULL;
        }
        g_VertexBufferSize = draw_data->TotalVtxCount + 5000;
        if (g_pd3dDevice->CreateVertexBuffer(g_VertexBufferSize * sizeof(ImDrawVert),
                                             D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_CUSTOMVERTEX,
                                             D3DPOOL_DEFAULT, &g_pVB, NULL) < 0)
            return;
    }
    if (!g_pIB || g_IndexBufferSize < draw_data->TotalIdxCount) {
        if (g_pIB) {
            g_pIB->Release();
            g_pIB = NULL;
        }
        g_IndexBufferSize = draw_data->TotalIdxCount + 10000;
        if (g_pd3dDevice->CreateIndexBuffer(g_IndexBufferSize * sizeof(ImDrawIdx),
                                            D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                            sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                            D3DPOOL_DEFAULT, &g_pIB, NULL) < 0)
            return;
    }

    // Backup the DX9 state
    IDirect3DStateBlock9 *d3d9_state_block = NULL;
    if (g_pd3dDevice->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block) < 0)
        return;

    // Backup the DX9 transform (DX9 documentation suggests that it is included in the StateBlock but it doesn't appear to)
    D3DMATRIX last_world, last_view, last_projection;
    g_pd3dDevice->GetTransform(D3DTS_WORLD, &last_world);
    g_pd3dDevice->GetTransform(D3DTS_VIEW, &last_view);
    g_pd3dDevice->GetTransform(D3DTS_PROJECTION, &last_projection);

    // Copy all vertices into a single contiguous buffer
    ImDrawVert *vtx_dst;
    ImDrawIdx *idx_dst;
    if (g_pVB->Lock(0, (UINT) (draw_data->TotalVtxCount * sizeof(ImDrawVert)), (void **) &vtx_dst,
                    D3DLOCK_DISCARD) < 0)
        return;
    if (g_pIB->Lock(0, (UINT) (draw_data->TotalIdxCount * sizeof(ImDrawIdx)), (void **) &idx_dst,
                    D3DLOCK_DISCARD) < 0)
        return;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    g_pVB->Unlock();
    g_pIB->Unlock();
    g_pd3dDevice->SetStreamSource(0, g_pVB, 0, sizeof(ImDrawVert));
    g_pd3dDevice->SetIndices(g_pIB);
    g_pd3dDevice->SetFVF(D3DFVF_CUSTOMVERTEX);

    // Setup viewport
    D3DVIEWPORT9 vp;
    vp.X = vp.Y = 0;
    vp.Width = (DWORD) draw_data->DisplaySize.x;
    vp.Height = (DWORD) draw_data->DisplaySize.y;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    g_pd3dDevice->SetPixelShader(nullptr);
    g_pd3dDevice->SetVertexShader(nullptr);

    D3DCAPS9 caps {};
    if (FAILED(g_pd3dDevice->GetDeviceCaps(&caps))) {
        caps.NumSimultaneousRTs = 0UL;
    }

    IDirect3DSurface9 *back_buffer = nullptr;
    IDirect3DSurface9 *depth_stencil = nullptr;
    IDirect3DSurface9 *render_targets[8];

    // save all previous render target state
    for (size_t target = 0; target < std::min(8UL, caps.NumSimultaneousRTs); target++) {
        if (FAILED(g_pd3dDevice->GetRenderTarget(target, &render_targets[target]))) {
            render_targets[target] = nullptr;
        }
    }

    // get the previous depth stencil
    if (FAILED(g_pd3dDevice->GetDepthStencilSurface(&depth_stencil))) {
        depth_stencil = nullptr;
    }

    // set the back buffer as the current render target
    if (SUCCEEDED(g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back_buffer))) {
        g_pd3dDevice->SetRenderTarget(0, back_buffer);
        g_pd3dDevice->SetDepthStencilSurface(nullptr);

        for (size_t target = 1; target < std::min(8UL, caps.NumSimultaneousRTs); target++) {
            g_pd3dDevice->SetRenderTarget(target, nullptr);
        }
    } else {
        back_buffer = nullptr;
    }

    // Setup render state: fixed-pipeline, alpha-blending, no face culling, no depth testing, shade mode (for gradient)
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, false);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, false);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, false);
    g_pd3dDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, true);
    g_pd3dDevice->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    g_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, false);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    // Setup orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    // Being agnostic of whether <d3dx9.h> or <DirectXMath.h> can be used, we aren't relying on D3DXMatrixIdentity()/D3DXMatrixOrthoOffCenterLH() or DirectX::XMMatrixIdentity()/DirectX::XMMatrixOrthographicOffCenterLH()
    {
        float L = draw_data->DisplayPos.x + 0.5f;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x + 0.5f;
        float T = draw_data->DisplayPos.y + 0.5f;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y + 0.5f;
        D3DMATRIX mat_identity = {{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}}};
        D3DMATRIX mat_projection =
                {{{
                          2.0f / (R - L), 0.0f, 0.0f, 0.0f,
                          0.0f, 2.0f / (T - B), 0.0f, 0.0f,
                          0.0f, 0.0f, 0.5f, 0.0f,
                          (L + R) / (L - R), (T + B) / (B - T), 0.5f, 1.0f
                  }}};
        g_pd3dDevice->SetTransform(D3DTS_WORLD, &mat_identity);
        g_pd3dDevice->SetTransform(D3DTS_VIEW, &mat_identity);
        g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &mat_projection);
    }

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                const RECT r = {(LONG) (pcmd->ClipRect.x - clip_off.x), (LONG) (pcmd->ClipRect.y - clip_off.y),
                                (LONG) (pcmd->ClipRect.z - clip_off.x), (LONG) (pcmd->ClipRect.w - clip_off.y)};
                auto texture = reinterpret_cast<IDirect3DBaseTexture9 *>(pcmd->TextureId);
                g_pd3dDevice->SetTexture(0, texture);
                g_pd3dDevice->SetScissorRect(&r);
                g_pd3dDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
                                                   pcmd->VtxOffset + global_vtx_offset, 0,
                                                   (UINT) cmd_list->VtxBuffer.Size,
                                                   pcmd->IdxOffset + global_idx_offset, pcmd->ElemCount / 3);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }

    if (back_buffer) {
        back_buffer->Release();
        back_buffer = nullptr;
    }

    // restore previous depth stencil
    if (depth_stencil) {
        g_pd3dDevice->SetDepthStencilSurface(depth_stencil);
        depth_stencil->Release();
        depth_stencil = nullptr;
    }

    // restore all render target state
    for (size_t target = 0; target < std::min(8UL, caps.NumSimultaneousRTs); target++) {
        auto render_target = render_targets[target];

        if (render_target) {
            g_pd3dDevice->SetRenderTarget(target, render_target);
            render_target->Release();
        }
    }

    // restore the DX9 transform
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &last_world);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &last_view);
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &last_projection);

    // restore the DX9 state
    d3d9_state_block->Apply();
    d3d9_state_block->Release();
}

bool ImGui_ImplDX9_Init(IDirect3DDevice9 *device) {

    // Setup back-end capabilities flags
    auto &io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_dx9";

    // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    g_pd3dDevice = device;
    g_pd3dDevice->AddRef();
    return true;
}

void ImGui_ImplDX9_Shutdown() {
    ImGui_ImplDX9_InvalidateDeviceObjects();

    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
}

static bool ImGui_ImplDX9_CreateFontsTexture() {
    // Build texture atlas
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels;
    int width, height, bytes_per_pixel;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

    // Upload texture to graphics system
    g_FontTexture = NULL;
    if (g_pd3dDevice->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &g_FontTexture, NULL) < 0)
        return false;
    D3DLOCKED_RECT tex_locked_rect;
    if (g_FontTexture->LockRect(0, &tex_locked_rect, NULL, 0) != D3D_OK)
        return false;
    for (int y = 0; y < height; y++)
        memcpy((unsigned char *) tex_locked_rect.pBits + tex_locked_rect.Pitch * y,
               pixels + (width * bytes_per_pixel) * y, (width * bytes_per_pixel));
    g_FontTexture->UnlockRect(0);

    // Store our identifier
    io.Fonts->TexID = (ImTextureID) g_FontTexture;

    return true;
}

bool ImGui_ImplDX9_CreateDeviceObjects() {
    if (!g_pd3dDevice) {
        return false;
    }
    return ImGui_ImplDX9_CreateFontsTexture();
}

void ImGui_ImplDX9_InvalidateDeviceObjects() {
    if (!g_pd3dDevice)
        return;
    if (g_pVB) {
        g_pVB->Release();
        g_pVB = NULL;
    }
    if (g_pIB) {
        g_pIB->Release();
        g_pIB = NULL;
    }
    if (g_FontTexture) {
        g_FontTexture->Release();
        g_FontTexture = NULL;
        ImGui::GetIO().Fonts->TexID = NULL;
    } // We copied g_pFontTextureView to io.Fonts->TexID so let's clear that as well.
}

void ImGui_ImplDX9_NewFrame() {
    if (!g_FontTexture) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    IDirect3DSwapChain9 *swap_chain = nullptr;
    if (SUCCEEDED(g_pd3dDevice->GetSwapChain(0, &swap_chain))) {
        auto &io = ImGui::GetIO();

        D3DPRESENT_PARAMETERS present_params {};

        if (SUCCEEDED(swap_chain->GetPresentParameters(&present_params))) {
            if (present_params.BackBufferWidth != 0 && present_params.BackBufferHeight != 0) {
                io.DisplaySize.x = static_cast<float>(present_params.BackBufferWidth);
                io.DisplaySize.y = static_cast<float>(present_params.BackBufferHeight);
            } else {
                RECT rect {};
                GetClientRect(present_params.hDeviceWindow, &rect);

                io.DisplaySize.x = static_cast<float>(rect.right - rect.left);
                io.DisplaySize.y = static_cast<float>(rect.bottom - rect.top);
            }
        }

        swap_chain->Release();
    }
}
