import bootstrap
import math
import unittest
import cv2
import numpy as np
from vision import RadarVision, Navigator, Pose, map_similarity, wrap

def marker():
    image=np.zeros((33,33),np.uint8)
    cv2.fillPoly(image,[np.array([[16,4],[23,24],[16,20],[9,24]],np.int32)],255)
    return image

def scene(icon,x=100,y=100,size=240):
    frame=np.zeros((size,size,3),np.uint8)
    r=icon.shape[0]//2
    frame[y-r:y+r+1,x-r:x+r+1]=cv2.cvtColor(icon,cv2.COLOR_GRAY2BGR)
    return frame

class VisionTests(unittest.TestCase):
    def setUp(self):
        self.vision=RadarVision(marker(),(0,0,200),(179,80,255),-math.pi/2)
    def test_position_and_heading_all_quadrants(self):
        for degrees in (0,40,90,150,180,250,300):
            matrix=cv2.getRotationMatrix2D((16,16),-degrees,1)
            icon=cv2.warpAffine(marker(),matrix,(33,33),flags=cv2.INTER_NEAREST)
            pose,reason=self.vision.locate(scene(icon,80,120))
            self.assertIsNotNone(pose,(degrees,reason))
            self.assertAlmostEqual(pose.x,80,delta=1)
            self.assertAlmostEqual(pose.y,120,delta=1)
            self.assertLess(abs(wrap(pose.heading-(-math.pi/2+math.radians(degrees)))),.18)
    def test_missing_player(self):
        self.assertIsNone(self.vision.locate(np.zeros((240,240,3),np.uint8))[0])
    def test_duplicate_players_rejected(self):
        a=scene(marker(),60,60)
        b=scene(marker(),170,170)
        self.assertIsNone(self.vision.locate(np.maximum(a,b))[0])
    def test_symmetric_marker_rejected(self):
        circle=np.zeros((33,33),np.uint8)
        cv2.circle(circle,(16,16),7,255,-1)
        model=RadarVision(circle,(0,0,200),(179,80,255),0)
        self.assertIsNone(model.locate(scene(circle))[0])
    def test_map_change(self):
        rng=np.random.default_rng(11)
        frame=rng.integers(0,256,(150,150,3),dtype=np.uint8)
        self.assertGreater(map_similarity(frame,frame),.99)
        self.assertLess(map_similarity(frame,np.flip(frame,0).copy()),.90)

class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.nav=Navigator([[20,20],[100,20],[100,100],[20,100]])
    def stable(self,pose,now=0):
        result=None
        for i in range(4): result=self.nav.update(pose,now+i*.1)
        return result
    def test_requires_stable_pose(self):
        self.assertFalse(self.nav.update(Pose(25,20,0,1),0)[0])
    def test_forward_aligned(self):
        self.assertTrue(self.stable(Pose(25,20,0,1))[0])
    def test_turn_before_walking(self):
        walking,error,_=self.stable(Pose(25,20,math.pi,1))
        self.assertFalse(walking)
        self.assertGreater(abs(error),3)
    def test_respawn_does_not_cross_map(self):
        self.stable(Pose(25,20,0,1))
        self.assertFalse(self.nav.update(Pose(60,60,0,1),1)[0])
        self.assertFalse(self.stable(Pose(60,60,0,1),2)[0])
        self.assertIsNone(self.nav.target)
    def test_stuck_stops(self):
        p=Pose(25,20,0,1)
        self.stable(p)
        with self.assertRaises(RuntimeError): self.nav.update(p,4)
    def test_progress_resets_stuck_timer(self):
        self.stable(Pose(25,20,0,1))
        self.assertTrue(self.nav.update(Pose(30,20,0,1),2)[0])
        self.assertTrue(self.nav.update(Pose(35,20,0,1),4)[0])

if __name__=="__main__":
    unittest.main(verbosity=2)
