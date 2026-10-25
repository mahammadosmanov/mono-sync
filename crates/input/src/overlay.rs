//! A cursor drawn by mono-sync itself, for Windows PCs with no mouse attached.
//!
//! Windows draws no pointer at all when it sees no mouse device, so a cursor that
//! we move with injected input stays invisible. This is a small click-through,
//! always-on-top window showing an arrow at the cursor position. Other platforms
//! get a no-op with the same API.

#[cfg(windows)]
mod imp {
    use std::{
        mem::{size_of, zeroed},
        ptr::{null, null_mut},
        sync::{
            atomic::{AtomicIsize, Ordering},
            Arc,
        },
        thread,
    };
    use winapi::{
        shared::{minwindef::*, windef::*},
        um::{libloaderapi::GetModuleHandleW, wingdi::*, winuser::*},
    };

    // 'X' outline, '.' fill. Classic 12x19 arrow, hotspot at the top-left pixel.
    const ARROW: [&str; 19] = [
        "X",
        "XX",
        "X.X",
        "X..X",
        "X...X",
        "X....X",
        "X.....X",
        "X......X",
        "X.......X",
        "X........X",
        "X.....XXXXX",
        "X..X..X",
        "X.X X..X",
        "XX  X..X",
        "X    X..X",
        "     X..X",
        "      X..X",
        "      X..X",
        "       XX",
    ];
    const W: i32 = 12;
    const H: i32 = 19;

    pub struct Overlay {
        hwnd: Arc<AtomicIsize>,
    }

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(Some(0)).collect()
    }

    unsafe extern "system" fn wndproc(h: HWND, m: UINT, w: WPARAM, l: LPARAM) -> LRESULT {
        DefWindowProcW(h, m, w, l)
    }

    /// Renders the arrow into the layered window (per-pixel alpha).
    unsafe fn paint(hw: HWND) {
        let screen = GetDC(null_mut());
        let mem = CreateCompatibleDC(screen);
        let mut bi: BITMAPINFO = zeroed();
        bi.bmiHeader = BITMAPINFOHEADER {
            biSize: size_of::<BITMAPINFOHEADER>() as u32,
            biWidth: W,
            biHeight: -H, // top-down
            biPlanes: 1,
            biBitCount: 32,
            biCompression: BI_RGB,
            ..zeroed()
        };
        let mut bits: *mut winapi::ctypes::c_void = null_mut();
        let bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
        if !bmp.is_null() && !bits.is_null() {
            let old = SelectObject(mem, bmp as _);
            let px = std::slice::from_raw_parts_mut(bits as *mut u32, (W * H) as usize);
            for (y, row) in ARROW.iter().enumerate() {
                for (x, c) in row.chars().enumerate() {
                    px[y * W as usize + x] = match c {
                        'X' => 0xFF00_0000,
                        '.' => 0xFFFF_FFFF,
                        _ => 0,
                    };
                }
            }
            let mut size = SIZE { cx: W, cy: H };
            let mut src = POINT { x: 0, y: 0 };
            let mut blend = BLENDFUNCTION {
                BlendOp: AC_SRC_OVER,
                BlendFlags: 0,
                SourceConstantAlpha: 255,
                AlphaFormat: AC_SRC_ALPHA,
            };
            UpdateLayeredWindow(
                hw,
                screen,
                null_mut(),
                &mut size,
                mem,
                &mut src,
                0,
                &mut blend,
                ULW_ALPHA,
            );
            SelectObject(mem, old);
        }
        if !bmp.is_null() {
            DeleteObject(bmp as _);
        }
        DeleteDC(mem);
        ReleaseDC(null_mut(), screen);
    }

    impl Overlay {
        pub fn start() -> Overlay {
            let hwnd = Arc::new(AtomicIsize::new(0));
            let h2 = hwnd.clone();
            thread::spawn(move || unsafe {
                let hinst = GetModuleHandleW(null());
                let class = wide("MonoSyncCursor");
                let wc = WNDCLASSW {
                    style: 0,
                    lpfnWndProc: Some(wndproc),
                    cbClsExtra: 0,
                    cbWndExtra: 0,
                    hInstance: hinst,
                    hIcon: null_mut(),
                    hCursor: null_mut(),
                    hbrBackground: null_mut(),
                    lpszMenuName: null(),
                    lpszClassName: class.as_ptr(),
                };
                RegisterClassW(&wc);
                let ex = WS_EX_LAYERED
                    | WS_EX_TRANSPARENT
                    | WS_EX_TOPMOST
                    | WS_EX_TOOLWINDOW
                    | WS_EX_NOACTIVATE;
                let hw = CreateWindowExW(
                    ex,
                    class.as_ptr(),
                    class.as_ptr(),
                    WS_POPUP,
                    0,
                    0,
                    W,
                    H,
                    null_mut(),
                    null_mut(),
                    hinst,
                    null_mut(),
                );
                if hw.is_null() {
                    return;
                }
                paint(hw);
                h2.store(hw as isize, Ordering::SeqCst);
                let mut msg: MSG = zeroed();
                while GetMessageW(&mut msg, null_mut(), 0, 0) > 0 {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            });
            Overlay { hwnd }
        }

        fn hw(&self) -> Option<HWND> {
            let h = self.hwnd.load(Ordering::SeqCst);
            (h != 0).then_some(h as HWND)
        }

        pub fn show_at(&self, x: i32, y: i32) {
            if let Some(hw) = self.hw() {
                // SAFETY: valid window handle owned by the overlay thread.
                unsafe {
                    SetWindowPos(
                        hw,
                        HWND_TOPMOST,
                        x,
                        y,
                        0,
                        0,
                        SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW,
                    );
                }
            }
        }

        pub fn hide(&self) {
            if let Some(hw) = self.hw() {
                // SAFETY: valid window handle owned by the overlay thread.
                unsafe {
                    ShowWindow(hw, SW_HIDE);
                }
            }
        }
    }

    /// Does Windows know about a mouse? Without one it draws no pointer.
    pub fn mouse_present() -> bool {
        // SAFETY: plain value query.
        unsafe { GetSystemMetrics(SM_MOUSEPRESENT) != 0 }
    }
}

#[cfg(not(windows))]
mod imp {
    pub struct Overlay;
    impl Overlay {
        pub fn start() -> Overlay {
            Overlay
        }
        pub fn show_at(&self, _x: i32, _y: i32) {}
        pub fn hide(&self) {}
    }
    pub fn mouse_present() -> bool {
        true
    }
}

pub use imp::{mouse_present, Overlay};
