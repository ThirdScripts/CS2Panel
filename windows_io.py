"""Standard Windows input and radar-only capture."""
import ctypes as C
from ctypes import wintypes as W
import os
import time
import threading
import numpy as np
import mss

user32=C.WinDLL("user32",use_last_error=True)
kernel32=C.WinDLL("kernel32",use_last_error=True)
user32.GetForegroundWindow.restype=W.HWND
user32.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
kernel32.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]
kernel32.OpenProcess.restype=W.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
kernel32.CloseHandle.argtypes=[W.HANDLE]
user32.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
user32.ClientToScreen.argtypes=[W.HWND,C.POINTER(W.POINT)]
user32.GetAsyncKeyState.argtypes=[C.c_int]
user32.GetAsyncKeyState.restype=C.c_short
ULONG_PTR=C.c_size_t
class MOUSEINPUT(C.Structure):
    _fields_=[("dx",W.LONG),("dy",W.LONG),("mouseData",W.DWORD),("dwFlags",W.DWORD),("time",W.DWORD),("dwExtraInfo",ULONG_PTR)]
class KEYBDINPUT(C.Structure):
    _fields_=[("wVk",W.WORD),("wScan",W.WORD),("dwFlags",W.DWORD),("time",W.DWORD),("dwExtraInfo",ULONG_PTR)]
class HARDWAREINPUT(C.Structure):
    _fields_=[("uMsg",W.DWORD),("wParamL",W.WORD),("wParamH",W.WORD)]
class INPUTUNION(C.Union):
    _fields_=[("mi",MOUSEINPUT),("ki",KEYBDINPUT),("hi",HARDWAREINPUT)]
class INPUT(C.Structure):
    _anonymous_=("u",)
    _fields_=[("type",W.DWORD),("u",INPUTUNION)]
user32.SendInput.argtypes=[W.UINT,C.POINTER(INPUT),C.c_int]
user32.SendInput.restype=W.UINT

def enable_dpi():
    try: user32.SetProcessDpiAwarenessContext(C.c_void_p(-4))
    except AttributeError: user32.SetProcessDPIAware()

def foreground_cs2():
    hwnd=user32.GetForegroundWindow()
    pid=W.DWORD()
    user32.GetWindowThreadProcessId(hwnd,C.byref(pid))
    handle=kernel32.OpenProcess(0x1000,False,pid.value)
    if not handle: return None
    try:
        text=C.create_unicode_buffer(32768)
        size=W.DWORD(len(text))
        if not kernel32.QueryFullProcessImageNameW(handle,0,text,C.byref(size)): return None
        return hwnd if os.path.basename(text.value).lower()=="cs2.exe" else None
    finally: kernel32.CloseHandle(handle)

def pressed(vk):
    return bool(user32.GetAsyncKeyState(vk)&0x8000)

class GameInput:
    def __init__(self):
        self.held=False
        self.lock=threading.RLock()
        self.last_lease=0.
        self.done=threading.Event()
        threading.Thread(target=self.watchdog,daemon=True).start()
    def watchdog(self):
        while not self.done.wait(.04):
            with self.lock:
                if self.held and (time.monotonic()-self.last_lease>.45 or foreground_cs2() is None or pressed(0x79)):
                    try: self.walk(False)
                    except OSError: pass
    def close(self):
        self.done.set()
        self.walk(False)
    def _send(self,event,releasing=False):
        if not releasing and (foreground_cs2() is None or pressed(0x79)):
            raise RuntimeError("Ввод остановлен: фокус или F10")
        if user32.SendInput(1,C.byref(event),C.sizeof(INPUT))!=1:
            raise OSError(C.get_last_error(),"Windows отклонила SendInput")
    def walk(self,down):
        with self.lock:
            if down: self.last_lease=time.monotonic()
            if down==self.held: return
            event=INPUT(type=1,ki=KEYBDINPUT(0,0x11,8|(0 if down else 2),0,0))
            self._send(event,releasing=not down)
            self.held=down
    def turn(self,dx):
        if dx: self._send(INPUT(type=0,mi=MOUSEINPUT(int(dx),0,0,1,0,0)))
    def key(self,vk):
        self._send(INPUT(type=1,ki=KEYBDINPUT(vk,0,0,0,0)))
        try: time.sleep(.025)
        finally: self._send(INPUT(type=1,ki=KEYBDINPUT(vk,0,2,0,0)),releasing=True)
    def greet(self,text):
        self.walk(False)
        self.key(ord("Y"))
        time.sleep(.18)
        data=text.encode("utf-16-le")
        for i in range(0,len(data),2):
            code=int.from_bytes(data[i:i+2],"little")
            self._send(INPUT(type=1,ki=KEYBDINPUT(0,code,4,0,0)))
            self._send(INPUT(type=1,ki=KEYBDINPUT(0,code,6,0,0)),releasing=True)
        self.key(0x0D)

class RadarCapture:
    def __init__(self):
        self.capture=mss.mss()
    def grab(self,roi):
        hwnd=foreground_cs2()
        if hwnd is None: raise RuntimeError("CS2 должна быть на переднем плане")
        rect,origin=W.RECT(),W.POINT(0,0)
        if not user32.GetClientRect(hwnd,C.byref(rect)) or not user32.ClientToScreen(hwnd,C.byref(origin)):
            raise RuntimeError("Не удалось определить границы окна CS2")
        x,y,w,h=roi
        if x<0 or y<0 or w<30 or h<30 or x+w>rect.right or y+h>rect.bottom:
            raise ValueError("Область радара вне окна игры")
        frame=np.array(self.capture.grab(dict(left=origin.x+x,top=origin.y+y,width=w,height=h)))[:,:,:3].copy()
        if foreground_cs2()!=hwnd: raise RuntimeError("Фокус изменился во время захвата")
        return frame
    def close(self):
        self.capture.close()
