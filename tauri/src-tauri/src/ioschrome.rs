/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

//! The system furniture iOS draws over a game: the status bar, the home indicator, and
//! the edges its own gestures start from.
//!
//! All three are read from the view controller rather than set on it, and the controller
//! belongs to wry, so there is no subclass here to override them in. The implementations
//! are added to whatever class the controller turns out to be. iOS never lets an app take
//! the home gesture away outright; deferring it means the first swipe reaches the game and
//! only a second one leaves it.

use objc2::runtime::{AnyClass, AnyObject, Bool, Sel};
use objc2::{msg_send, sel};
use std::ffi::c_void;

/// UIRectEdgeAll.
const UI_RECT_EDGE_ALL: usize = 0xF;

extern "C" fn yes(_this: &AnyObject, _cmd: Sel) -> Bool {
    Bool::YES
}

extern "C" fn all_edges(_this: &AnyObject, _cmd: Sel) -> usize {
    UI_RECT_EDGE_ALL
}

/// Applies the three overrides to the application's root view controller and asks iOS to
/// read them again. Safe to call more than once, and a no-op if there is no window yet.
pub fn hide_system_chrome() {
    unsafe {
        let application: *mut AnyObject = msg_send![
            objc2::class!(UIApplication),
            sharedApplication
        ];
        if application.is_null() {
            return;
        }

        let window: *mut AnyObject = msg_send![application, keyWindow];
        if window.is_null() {
            return;
        }

        let controller: *mut AnyObject = msg_send![window, rootViewController];
        if controller.is_null() {
            return;
        }

        let class: *const AnyClass = msg_send![controller, class];
        let class = &*class;

        add_override(class, sel!(prefersStatusBarHidden), yes as *const c_void, b"B@:\0");
        add_override(class, sel!(prefersHomeIndicatorAutoHidden), yes as *const c_void, b"B@:\0");
        add_override(
            class,
            sel!(preferredScreenEdgesDeferringSystemGestures),
            all_edges as *const c_void,
            b"Q@:\0",
        );

        let _: () = msg_send![controller, setNeedsStatusBarAppearanceUpdate];
        let _: () = msg_send![controller, setNeedsUpdateOfHomeIndicatorAutoHidden];
        let _: () = msg_send![controller, setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    }
}

/// Replaces the class's implementation of one selector, adding it when the class does not
/// answer the selector itself.
unsafe fn add_override(class: &AnyClass, selector: Sel, imp: *const c_void, types: &[u8]) {
    extern "C" {
        fn class_replaceMethod(
            class: *const AnyClass,
            name: Sel,
            imp: *const c_void,
            types: *const u8,
        ) -> *const c_void;
    }

    class_replaceMethod(class as *const AnyClass, selector, imp, types.as_ptr());
}
