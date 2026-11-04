import bootstrap
import math
import unittest
import cv2
import numpy as np
from navigation import GridMap, PatrolNavigator
from vision import PlayerMarkerVision, Pose

class NavigationTests(unittest.TestCase):
    def test_route_uses_gap(self):
        mask=np.ones((90,90),np.uint8)*255
        mask[:65,42:48]=0
        grid=GridMap(mask)
        route=grid.path(grid.nearest((15,15)),grid.nearest((75,15)))
        self.assertGreater(max(y for x,y in route),65)
        self.assertTrue(all(grid.open((x//3,y//3)) for x,y in route))
        self.assertFalse(grid.visible((15,15),(75,15)))

    def test_disconnected(self):
        mask=np.ones((90,90),np.uint8)*255
        mask[:,42:48]=0
        grid=GridMap(mask)
        with self.assertRaises(RuntimeError):
            grid.path(grid.nearest((15,15)),grid.nearest((75,15)))

    def test_respawn_releases_movement(self):
        nav=PatrolNavigator(GridMap(np.ones((120,120),np.uint8)*255))
        for i in range(4): nav.update(Pose(30,30,0,1),i*.1)
        walking,angle,_=nav.update(Pose(90,90,0,1),.5)
        self.assertFalse(walking)
        self.assertEqual(angle,0)
        self.assertFalse(nav.route)

    def test_disc_requires_unique_direction(self):
        image=np.zeros((100,100,3),np.uint8)
        cv2.circle(image,(30,30),7,(0,255,255),-1)
        model=PlayerMarkerVision()
        self.assertIsNone(model.locate(image)[0])
        cv2.fillConvexPoly(image,np.array([[38,27],[43,30],[38,33]]), (255,255,255))
        pose,_=model.locate(image)
        self.assertIsNotNone(pose)
        self.assertAlmostEqual(pose.heading,0,places=2)
        image[50:100,50:100]=image[:50,:50]
        self.assertIsNone(model.locate(image)[0])

    def test_real_frames(self):
        paths=list((bootstrap.ROOT/'artifacts').glob('radar-square-*.png'))
        if not paths: self.skipTest('Local calibration frames unavailable')
        for path in paths:
            with self.subTest(frame=path.name):
                pose,_=PlayerMarkerVision().locate(cv2.imread(str(path)))
                self.assertIsNotNone(pose)
                self.assertTrue(math.isfinite(pose.heading))

if __name__=='__main__': unittest.main()
