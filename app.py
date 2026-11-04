"""Dust II radar-navigation laboratory. Run with: python app.py"""
import bootstrap
import json
import math
import queue
import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox
import cv2
import numpy as np
from PIL import Image, ImageTk
from vision import RadarVision, PlayerMarkerVision, Navigator, map_similarity
from navigation import GridMap, PatrolNavigator
from game_state import GameState
from windows_io import GameInput, RadarCapture, foreground_cs2, pressed, enable_dpi

PROFILE=bootstrap.ROOT/"profile"

class App:
    def __init__(self,root,demo=False):
        self.demo=demo
        self.root=root
        self.root.title("Dust II • Radar Navigator — experimental")
        self.root.geometry("1100x940")
        self.root.minsize(960,900)
        self.io=GameInput()
        self.game_sensor=None
        self.gsi_error=""
        if not demo:
            try:
                token=(PROFILE/"gsi-token.txt").read_text(encoding="utf8").strip()
                self.game_sensor=GameState(token)
            except Exception as e: self.gsi_error=str(e)
        self.closed=threading.Event()
        self.observing=threading.Event()
        self.frames=queue.Queue(maxsize=1)
        self.roi=(0,0,320,320)
        self.model=None
        self.reference=None
        self.template=None
        self.marker_heading=0.
        self.route=[]
        self.auto_route=True
        self.last_frame=None
        self.last_pose=None
        self.last_time=0.
        self.running=False
        self.armed=False
        self.last_log=0.
        self.nav=None
        self.edit_mode=None
        self.marker_center=None
        self.keys={}
        self.last_greet=-100.
        self.scale=1.
        self.hsv_low=(0,0,200)
        self.hsv_high=(179,80,255)
        self.build_ui()
        self.load_profile()
        self.worker=threading.Thread(target=self.observe_loop,daemon=True)
        if not demo: self.worker.start()
        self.root.protocol("WM_DELETE_WINDOW",self.close)
        self.root.after(30,self.tick)

    def build_ui(self):
        style=ttk.Style()
        style.theme_use("clam")
        self.root.configure(bg="#121b28")
        style.configure("TFrame",background="#121b28")
        style.configure("TLabel",background="#121b28",foreground="#e2edf7",font=("Segoe UI",10))
        style.configure("TLabelframe",background="#121b28",foreground="#e2edf7")
        style.configure("TLabelframe.Label",background="#121b28",foreground="#93c5fd")
        style.configure("TButton",padding=2,font=("Segoe UI",10))
        outer=ttk.Frame(self.root,padding=16)
        outer.pack(fill="both",expand=True)
        ttk.Label(outer,text="DUST II / RADAR NAVIGATOR",font=("Segoe UI",19,"bold")).pack(anchor="w")
        ttk.Label(outer,text="Позиция → направление → следующая точка → проверка движения").pack(anchor="w",pady=(2,12))
        body=ttk.Frame(outer)
        body.pack(fill="both",expand=True)
        left=ttk.Frame(body)
        left.pack(side="left",fill="both",expand=True)
        self.canvas=tk.Canvas(left,width=640,height=550,bg="#09111c",highlightthickness=0)
        self.canvas.pack(fill="both",expand=True)
        self.canvas.bind("<Button-1>",self.canvas_click)
        self.canvas.bind("<Configure>",lambda e:self.draw())
        self.status=tk.StringVar(value="Наблюдение выключено. Сначала прочитай README.")
        ttk.Label(left,textvariable=self.status,wraplength=640).pack(anchor="w",pady=10)
        self.pose_text=tk.StringVar(value="Координаты: —   Направление: —")
        ttk.Label(left,textvariable=self.pose_text).pack(anchor="w")
        right=ttk.Frame(body,padding=(16,0,0,0))
        right.pack(side="right",fill="y")
        ttk.Label(right,text="1. Область радара в окне CS2").pack(anchor="w")
        self.roi_text=tk.StringVar(value="0, 0, 320, 320")
        ttk.Entry(right,textvariable=self.roi_text,width=35).pack(fill="x",pady=5)
        ttk.Label(right,text="x, y, ширина, высота • физические пиксели").pack(anchor="w")
        ttk.Button(right,text="Применить область",command=self.apply_roi).pack(fill="x",pady=4)
        ttk.Button(right,text="Наблюдать / остановить захват",command=self.toggle_observe).pack(fill="x",pady=4)
        ttk.Button(right,text="Зафиксировать кадр для настройки",command=self.freeze).pack(fill="x",pady=4)
        ttk.Separator(right).pack(fill="x",pady=10)
        ttk.Label(right,text="2. Распознавание маркера").pack(anchor="w")
        self.hsv_text=tk.StringVar(value="0,0,200 / 179,80,255")
        ttk.Entry(right,textvariable=self.hsv_text).pack(fill="x",pady=5)
        ttk.Label(right,text="HSV: нижняя граница / верхняя граница").pack(anchor="w")
        ttk.Button(right,text="Указать центр и направление",command=self.start_marker).pack(fill="x",pady=5)
        ttk.Button(right,text="Принять фон радара",command=self.set_reference).pack(fill="x",pady=4)
        ttk.Separator(right).pack(fill="x",pady=10)
        ttk.Label(right,text="3. Маршрут по карте").pack(anchor="w")
        ttk.Button(right,text="Автоматическое распознавание и путь",command=self.enable_auto).pack(fill="x",pady=4)
        ttk.Button(right,text="Рисовать маршрут кликами",command=self.start_route).pack(fill="x",pady=4)
        ttk.Button(right,text="Удалить последнюю точку",command=self.undo_point).pack(fill="x",pady=4)
        ttk.Button(right,text="Сохранить профиль Dust II",command=self.save_profile).pack(fill="x",pady=4)
        ttk.Separator(right).pack(fill="x",pady=10)
        self.allow=tk.BooleanVar(value=False)
        ttk.Checkbutton(right,text="Разрешить управление движением",variable=self.allow,command=self.stop).pack(anchor="w")
        ttk.Button(right,text="Старт после переключения в CS2",command=self.arm).pack(fill="x")
        ttk.Label(right,text="F8 — старт / стоп   •   F10 — стоп").pack(anchor="w",pady=4)
        self.greeting=tk.StringVar(value="Всем удачи, хорошей игры!")
        ttk.Entry(right,textvariable=self.greeting).pack(fill="x",pady=5)
        ttk.Label(right,text="F6 — сообщение в общий чат (клавиша Y)").pack(anchor="w")
        ttk.Label(right,text="Проверка VAC не выполнена.\nНастройка радара и маршрута обязательна.\nДвижение при запуске выключено.",wraplength=310).pack(anchor="w",pady=12)

    def stop(self,reason=None):
        self.running=False
        self.armed=False
        if self.nav: self.nav.reset()
        self.io.walk(False)
        if reason: self.status.set(reason)

    def arm(self):
        if self.allow.get() and self.model is not None and self.reference is not None:
            self.armed=True
            self.status.set("Ожидаю переключения в CS2 для запуска")
        else: self.status.set("Нужны профиль и разрешение движения")

    def apply_roi(self):
        try:
            roi=tuple(int(x.strip()) for x in self.roi_text.get().split(","))
            if len(roi)!=4 or min(roi[:2])<0 or not all(64<=n<=800 for n in roi[2:]):
                raise ValueError("Нужны x,y >= 0 и размеры от 64 до 800.")
            self.stop()
            self.observing.clear()
            self.roi=roi
            self.model=self.reference=self.template=self.last_frame=None
            self.route=[]
            self.status.set("Область изменена. Шаблон и маршрут сброшены.")
            self.draw()
        except Exception as e: messagebox.showerror("Область радара",str(e))

    def toggle_observe(self):
        if self.demo:
            self.status.set("Демонстрация: захват игры и ввод отключены")
            return
        self.stop()
        self.edit_mode=None
        if self.observing.is_set():
            self.observing.clear()
            self.status.set("Наблюдение остановлено")
        else:
            self.observing.set()
            self.status.set("Переключись в CS2. Захватывается только область радара.")

    def freeze(self):
        self.stop()
        self.observing.clear()
        self.edit_mode=None
        self.status.set("Кадр зафиксирован" if self.last_frame is not None else "Нет кадра. Включи наблюдение и переключись в CS2.")

    def start_marker(self):
        self.freeze()
        if self.last_frame is not None:
            self.edit_mode="center"
            self.status.set("Кликни точно в центр своего маркера. Затем укажи, куда он смотрит.")

    def set_reference(self):
        self.freeze()
        if self.last_frame is not None:
            self.reference=self.last_frame.copy()
            self.status.set("Фон сохранён. Масштаб, положение и вращение радара менять нельзя.")

    def start_route(self):
        self.freeze()
        self.auto_route=False
        if self.last_frame is not None:
            self.edit_mode="route"
            self.status.set("Кликай по центрам проходимых коридоров. Последняя точка соединится с первой.")

    def enable_auto(self):
        self.freeze()
        self.model=PlayerMarkerVision()
        self.auto_route=True
        self.route=[]
        self.status.set("Автоматический поиск своего маркера и пути включён. Нужен фон квадратного радара.")

    def undo_point(self):
        self.stop()
        if self.route: self.route.pop()
        self.draw()

    def canvas_click(self,event):
        if self.last_frame is None or not self.edit_mode: return
        x,y=int(event.x/self.scale),int(event.y/self.scale)
        h,w=self.last_frame.shape[:2]
        if not (0<=x<w and 0<=y<h): return
        if self.edit_mode=="route":
            self.route.append([x,y])
        elif self.edit_mode=="center":
            self.marker_center=(x,y)
            self.edit_mode="heading"
            self.status.set("Теперь кликни по направлению взгляда, на 15–40 пикселей от центра.")
        elif self.edit_mode=="heading":
            try:
                cx,cy=self.marker_center
                if math.hypot(x-cx,y-cy)<10: raise ValueError("Укажи направление дальше от центра.")
                radius=16
                if cx<radius or cy<radius or cx+radius>=w or cy+radius>=h:
                    raise ValueError("Маркер слишком близко к краю радара.")
                low,high=self.hsv_text.get().split("/")
                low=tuple(map(int,low.split(","))); high=tuple(map(int,high.split(",")))
                if len(low)!=3 or len(high)!=3 or any(not 0<=a<=b<=limit for a,b,limit in zip(low,high,(179,255,255))):
                    raise ValueError("Неверные границы HSV.")
                crop=self.last_frame[cy-radius:cy+radius+1,cx-radius:cx+radius+1]
                template=cv2.inRange(cv2.cvtColor(crop,cv2.COLOR_BGR2HSV),np.array(low,np.uint8),np.array(high,np.uint8))
                heading=math.atan2(y-cy,x-cx)
                model=RadarVision(template,low,high,heading)
                pose,reason=model.locate(self.last_frame)
                if pose is None or math.hypot(pose.x-cx,pose.y-cy)>3:
                    raise ValueError("Самопроверка шаблона не пройдена: "+reason+". Уточни HSV/центр.")
                self.hsv_low,self.hsv_high=low,high
                self.marker_heading,self.template,self.model=heading,template,model
                self.last_pose=pose
                self.edit_mode=None
                self.status.set("Шаблон распознан на исходном кадре. Проверь другие положения и углы.")
            except Exception as e: messagebox.showerror("Калибровка",str(e))
        self.draw()

    def save_profile(self):
        try:
            if self.model is None or self.reference is None: raise ValueError("Сначала задай маркер и фон.")
            if not self.auto_route: Navigator(self.route)
            PROFILE.mkdir(exist_ok=True)
            if self.template is not None: cv2.imwrite(str(PROFILE/"marker.png"),self.template)
            cv2.imwrite(str(PROFILE/"radar.png"),self.reference)
            data=dict(roi=self.roi,hsv_low=self.hsv_low,hsv_high=self.hsv_high,heading=self.marker_heading,
                      route=self.route,greeting=self.greeting.get(),auto_route=self.auto_route,
                      detector="disc" if isinstance(self.model,PlayerMarkerVision) else "template")
            (PROFILE/"dust2.json").write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding="utf8")
            self.status.set("Профиль сохранён")
        except Exception as e: messagebox.showerror("Профиль",str(e))

    def load_profile(self):
        file=PROFILE/"dust2.json"
        if not file.exists(): return
        try:
            data=json.loads(file.read_text(encoding="utf8"))
            self.roi=tuple(map(int,data["roi"]))
            if len(self.roi)!=4 or min(self.roi[:2])<0 or not all(64<=v<=800 for v in self.roi[2:]):
                raise ValueError("Неверная область.")
            self.hsv_low,self.hsv_high=data["hsv_low"],data["hsv_high"]
            self.marker_heading=float(data["heading"])
            self.template=cv2.imread(str(PROFILE/"marker.png"),0) if data.get("detector")!="disc" else None
            self.reference=cv2.imread(str(PROFILE/"radar.png"))
            if self.reference is None: raise ValueError("Фон профиля отсутствует.")
            if self.reference.shape[:2]!=(self.roi[3],self.roi[2]): raise ValueError("Размер фона не совпадает.")
            self.model=PlayerMarkerVision() if data.get("detector")=="disc" else RadarVision(self.template,self.hsv_low,self.hsv_high,self.marker_heading)
            self.auto_route=data.get("auto_route",False)
            self.route=data["route"]
            if not self.auto_route: Navigator(self.route)
            self.roi_text.set(", ".join(map(str,self.roi)))
            self.hsv_text.set(",".join(map(str,self.hsv_low))+" / "+",".join(map(str,self.hsv_high)))
            self.greeting.set(data.get("greeting","Всем удачи!"))
            self.last_frame=self.reference.copy()
            self.status.set("Профиль загружен. Наблюдение и движение выключены.")
            self.root.after(100,self.draw)
        except Exception as e:
            self.model=self.reference=self.template=None
            self.status.set("Профиль не загружен: "+str(e))

    def observe_loop(self):
        capture=None
        try:
            capture=RadarCapture()
            while not self.closed.is_set():
                if not self.observing.wait(.1): continue
                started=time.monotonic()
                try:
                    frame=capture.grab(self.roi)
                    model,reference=self.model,self.reference
                    pose,reason=model.locate(frame) if model else (None,"Нужна калибровка маркера")
                    similarity=map_similarity(reference,frame) if reference is not None else 0.
                    if reference is not None and similarity<.90:
                        pose=None
                        reason=f"Фон радара изменился ({similarity:.2f})"
                    result=(started,frame,pose,reason)
                except Exception as e: result=(started,None,None,str(e))
                try: self.frames.get_nowait()
                except queue.Empty: pass
                self.frames.put_nowait(result)
                self.closed.wait(max(.01,.10-(time.monotonic()-started)))
        except Exception as e:
            try: self.frames.put_nowait((time.monotonic(),None,None,"Захват не запущен: "+str(e)))
            except queue.Full: pass
        finally:
            if capture: capture.close()

    def edge(self,vk):
        down=pressed(vk)
        result=down and not self.keys.get(vk,False)
        self.keys[vk]=down
        return result

    def tick(self):
        if self.demo: return
        try:
            now=time.monotonic()
            focused=foreground_cs2() is not None
            f6,f8,f10=self.edge(0x75),self.edge(0x77),self.edge(0x79)
            if f10 or (self.running and (not focused or any(pressed(k) for k in (0x1B,0x09,0xC0,0x59,0x55)))):
                self.stop("Остановлено")
            if not self.allow.get() and self.running: self.stop()
            if (f8 or self.armed) and focused:
                self.armed=False
                if self.running: self.stop("Остановлено по F8")
                elif self.allow.get() and self.model is not None and self.reference is not None:
                    self.nav=PatrolNavigator(GridMap.from_radar(self.reference)) if self.auto_route else Navigator(self.route)
                    self.running=True
                    self.observing.set()
                    self.status.set("Запрос запуска: ожидание свежей позиции")
                else: self.status.set("Нужны калибровка, фон, маршрут и разрешение движения")
            if f6 and focused and now-self.last_greet>=5:
                self.stop("Приветствие по F6")
                text=self.greeting.get().strip()
                if not text or len(text)>120 or any(ord(c)<32 for c in text):
                    raise ValueError("Сообщение: 1–120 символов без переводов строк.")
                self.last_greet=now
                self.io.greet(text)
            try:
                stamp,frame,pose,reason=self.frames.get_nowait()
                if self.observing.is_set():
                    self.last_time=stamp
                    self.last_pose=pose
                    if frame is not None: self.last_frame=frame
                    if self.running or self.observing.is_set(): self.status.set(reason)
                    if pose:
                        self.pose_text.set(f"x={pose.x:.1f}, y={pose.y:.1f}   угол={math.degrees(pose.heading):.0f}°   оценка={pose.confidence:.2f}")
                    else: self.pose_text.set("Позиция не подтверждена")
                    if self.running:
                        game_ok,game_reason=self.game_sensor.allowed() if self.game_sensor else (False,"GSI не подключена: "+self.gsi_error)
                        if not game_ok:
                            self.io.walk(False)
                            self.nav.reset()
                            self.status.set(game_reason)
                        elif pose is None or frame is None or now-stamp>.35:
                            self.io.walk(False)
                            self.nav.reset()
                        else:
                            walking,error,message=self.nav.update(pose,stamp)
                            if self.auto_route: self.route=list(self.nav.route)
                            self.io.walk(walking)
                            self.io.turn(max(-60,min(60,round(error*100))))
                            self.status.set(message)
                    self.draw()
                    if now-self.last_log>=1:
                        self.last_log=now
                        print(json.dumps(dict(time=round(now,2),running=self.running,status=self.status.get(),
                                              pose=None if pose is None else [pose.x,pose.y,pose.heading]),ensure_ascii=False),flush=True)
            except queue.Empty: pass
            if self.running and now-self.last_time>.35:
                self.io.walk(False)
                self.nav.reset()
        except Exception as e:
            try: self.stop("Остановка: "+str(e))
            except Exception: self.running=False
        if not self.closed.is_set(): self.root.after(30,self.tick)

    def draw(self):
        self.canvas.delete("all")
        if self.last_frame is None:
            self.canvas.create_text(25,35,anchor="nw",text="Здесь появится радар CS2\n\n1. Укажи область радара\n2. Включи наблюдение\n3. Переключись в игру\n4. Вернись и зафиксируй кадр",fill="#94a3b8",font=("Segoe UI",14))
            return
        h,w=self.last_frame.shape[:2]
        self.scale=min(max(1,self.canvas.winfo_width())/w,max(1,self.canvas.winfo_height())/h)
        image=Image.fromarray(cv2.cvtColor(self.last_frame,cv2.COLOR_BGR2RGB)).resize((max(1,int(w*self.scale)),max(1,int(h*self.scale))))
        self.photo=ImageTk.PhotoImage(image)
        self.canvas.create_image(0,0,anchor="nw",image=self.photo)
        if len(self.route)>1:
            points=[v*self.scale for p in (self.route if self.auto_route else self.route+[self.route[0]]) for v in p]
            self.canvas.create_line(*points,fill="#38bdf8",width=2)
        for i,(x,y) in enumerate(self.route):
            x,y=x*self.scale,y*self.scale
            self.canvas.create_oval(x-3,y-3,x+3,y+3,fill="#38bdf8")
            self.canvas.create_text(x+7,y-8,text=str(i+1),fill="white",anchor="w")
        if self.last_pose:
            p=self.last_pose
            x,y=p.x*self.scale,p.y*self.scale
            self.canvas.create_oval(x-7,y-7,x+7,y+7,outline="#4ade80",width=2)
            self.canvas.create_line(x,y,x+30*math.cos(p.heading),y+30*math.sin(p.heading),fill="#4ade80",width=3,arrow="last")

    def close(self):
        self.closed.set()
        self.observing.clear()
        try:
            self.stop()
            if self.game_sensor: self.game_sensor.close()
        finally:
            self.io.close()
            self.root.destroy()

if __name__=="__main__":
    enable_dpi()
    root=tk.Tk()
    import sys
    app=App(root,demo="--demo" in sys.argv)
    if app.demo:
        from test_vision import marker,scene
        app.last_frame=scene(marker(),100,110,320)
        app.model=RadarVision(marker(),(0,0,200),(179,80,255),-math.pi/2)
        app.last_pose,_=app.model.locate(app.last_frame)
        app.route=[[100,110],[100,50],[250,50],[250,250],[100,250]]
        app.status.set("Демонстрационный кадр. Это не снимок Dust II; ввод и захват отключены.")
        root.after(150,app.draw)
    root.mainloop()
