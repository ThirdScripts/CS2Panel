"""Receive CS2 Game State Integration on loopback; fail closed when stale."""
import hmac
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

def assess(payload):
    if not isinstance(payload,dict): return False,"Нет состояния GSI"
    provider=payload.get("provider",{})
    player=payload.get("player",{})
    game_map=payload.get("map",{})
    if not all(isinstance(x,dict) for x in (provider,player,game_map)):
        return False,"Некорректное состояние GSI"
    if provider.get("appid")!=730: return False,"Источник GSI не CS2"
    if game_map.get("name")!="de_dust2": return False,"Нужна карта Dust II"
    if game_map.get("phase") not in ("live","warmup"): return False,"Матч не активен"
    if not provider.get("steamid") or provider.get("steamid")!=player.get("steamid"):
        return False,"Наблюдение за другим игроком"
    if player.get("activity")!="playing": return False,"Игрок не в игре"
    state=player.get("state",{})
    health=state.get("health",0) if isinstance(state,dict) else 0
    if isinstance(health,bool) or not isinstance(health,(int,float)) or not 0<health<=100:
        return False,"Игрок погиб или здоровье неизвестно"
    return True,"GSI: игрок жив на Dust II"

class GameState:
    def __init__(self,token,port=31987):
        self.token=token
        self.lock=threading.Lock()
        self.payload=None
        self.received=0.
        parent=self
        class Handler(BaseHTTPRequestHandler):
            def setup(self):
                super().setup()
                self.connection.settimeout(1.)
            def do_POST(self):
                try:
                    length=int(self.headers.get("Content-Length","0"))
                    if not 0<length<=131072: raise ValueError("Invalid length")
                    payload=json.loads(self.rfile.read(length))
                    if not isinstance(payload,dict): raise ValueError("Invalid payload")
                    auth=payload.get("auth",{})
                    supplied=auth.get("token","") if isinstance(auth,dict) else ""
                    if not isinstance(supplied,str) or not hmac.compare_digest(supplied,parent.token):
                        self.send_error(403)
                        return
                    with parent.lock:
                        parent.payload=payload
                        parent.received=time.monotonic()
                    self.send_response(200)
                    self.send_header("Content-Length","0")
                    self.end_headers()
                except (ValueError,TypeError,TimeoutError):
                    self.send_error(400)
            def log_message(self,*args): pass
        self.server=ThreadingHTTPServer(("127.0.0.1",port),Handler)
        self.server.daemon_threads=True
        self.thread=threading.Thread(target=self.server.serve_forever,kwargs={"poll_interval":.1},daemon=True)
        self.thread.start()
    def allowed(self):
        with self.lock:
            if time.monotonic()-self.received>1.5:
                return False,"Нет свежего GSI — запусти CS2 после установки конфигурации"
            return assess(self.payload)
    def close(self):
        self.server.shutdown()
        self.server.server_close()
