import unittest
from copy import deepcopy
from game_state import assess, GameState
import json, urllib.request, urllib.error, time

class GameStateTests(unittest.TestCase):
    def setUp(self):
        self.payload={"provider":{"appid":730,"steamid":"test"},"player":{"steamid":"test","activity":"playing","state":{"health":100}},"map":{"name":"de_dust2","phase":"live"}}
    def test_live(self): self.assertTrue(assess(self.payload)[0])
    def test_dead(self):
        self.payload["player"]["state"]["health"]=0
        self.assertFalse(assess(self.payload)[0])
    def test_spectator(self):
        self.payload["player"]["steamid"]="other"
        self.assertFalse(assess(self.payload)[0])
    def test_wrong_map(self):
        self.payload["map"]["name"]="de_mirage"
        self.assertFalse(assess(self.payload)[0])
    def test_menu(self):
        self.payload["player"]["activity"]="menu"
        self.assertFalse(assess(self.payload)[0])
    def test_malformed(self):
        for payload in ([],None,{"player":None},{}):
            self.assertFalse(assess(payload)[0])
    def test_local_endpoint_auth_and_expiry(self):
        server=GameState("unit-test",port=0)
        try:
            self.assertFalse(server.allowed()[0])
            url="http://127.0.0.1:"+str(server.server.server_address[1])+"/"
            self.payload["auth"]={"token":"wrong"}
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(urllib.request.Request(url,data=json.dumps(self.payload).encode()),timeout=2)
            self.assertFalse(server.allowed()[0])
            self.payload["auth"]["token"]="unit-test"
            with urllib.request.urlopen(urllib.request.Request(url,data=json.dumps(self.payload).encode()),timeout=2) as response:
                self.assertEqual(response.status,200)
            self.assertTrue(server.allowed()[0])
            with server.lock: server.received=time.monotonic()-2
            self.assertFalse(server.allowed()[0])
        finally: server.close()

if __name__=="__main__": unittest.main()
