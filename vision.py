"""Radar-space localization and feedback navigation; no game-memory access."""
from dataclasses import dataclass
import math
import cv2
import numpy as np

@dataclass(frozen=True)
class Pose:
    x: float
    y: float
    heading: float
    confidence: float

def wrap(angle):
    return (angle + math.pi) % (2 * math.pi) - math.pi

class RadarVision:
    def __init__(self, template, hsv_low, hsv_high, heading, threshold=.78):
        if template.ndim != 2 or not 12 <= np.count_nonzero(template) <= 1000:
            raise ValueError("Шаблон должен содержать 12–1000 пикселей маркера.")
        self.low, self.high = np.array(hsv_low,np.uint8), np.array(hsv_high,np.uint8)
        self.threshold = threshold
        self.bank = []
        size = template.shape[0]
        if template.shape != (size,size) or size % 2 != 1:
            raise ValueError("Шаблон должен быть квадратным с нечётной стороной.")
        center = ((size-1)/2,(size-1)/2)
        for degrees in range(0,360,10):
            matrix = cv2.getRotationMatrix2D(center,-degrees,1)
            mask = cv2.warpAffine(template,matrix,(size,size),flags=cv2.INTER_NEAREST)
            self.bank.append((mask.astype(np.float32)/255,wrap(heading+math.radians(degrees))))

    def locate(self, bgr):
        binary = (cv2.inRange(cv2.cvtColor(bgr,cv2.COLOR_BGR2HSV),self.low,self.high)>0).astype(np.float32)
        size = self.bank[0][0].shape[0]
        if min(binary.shape)<size:
            return None,"Радар меньше шаблона"
        counts = cv2.matchTemplate(binary,np.ones((size,size),np.float32),cv2.TM_CCORR)
        matches = []
        for template,heading in self.bank:
            intersection = cv2.matchTemplate(binary,template,cv2.TM_CCORR)
            scores = intersection/np.maximum(counts+template.sum()-intersection,1)
            _,score,_,pos = cv2.minMaxLoc(scores)
            matches.append((score,pos,heading,scores))
        score,(x,y),heading,_ = max(matches,key=lambda m:m[0])
        if score<self.threshold:
            return None,f"Маркер не найден: {score:.2f}"
        second = 0.
        radius = max(4,size//3)
        for _,_,other_heading,scores in matches:
            candidate = scores.copy()
            if abs(wrap(other_heading-heading))<math.radians(35):
                candidate[max(0,y-radius):y+radius+1,max(0,x-radius):x+radius+1] = 0
            second=max(second,float(candidate.max()))
        if score-second<.09:
            return None,f"Неоднозначный маркер/направление: {score:.2f}/{second:.2f}"
        half=(size-1)/2
        return Pose(x+half,y+half,heading,score),"Позиция определена"

def map_similarity(reference,current):
    if reference.shape!=current.shape:
        return 0.
    def prepare(image):
        gray=cv2.cvtColor(image,cv2.COLOR_BGR2GRAY)
        return cv2.GaussianBlur(gray,(11,11),0).astype(np.float32).ravel()
    a,b=prepare(reference),prepare(current)
    a,b=a-a.mean(),b-b.mean()
    denom=np.linalg.norm(a)*np.linalg.norm(b)
    return float(np.dot(a,b)/denom) if denom>1e-6 else 0.

class Navigator:
    def __init__(self,route,arrival=4.,join_distance=9.):
        self.route=np.asarray(route,np.float64)
        if self.route.ndim!=2 or self.route.shape[1]!=2 or len(self.route)<3:
            raise ValueError("Нужно минимум 3 точки замкнутого маршрута.")
        if not np.isfinite(self.route).all():
            raise ValueError("Неверные координаты маршрута.")
        self.arrival,self.join_distance=arrival,join_distance
        self.reset()

    def reset(self):
        self.target=self.last_pose=self.progress_pos=self.progress_time=None
        self.stable=0

    def update(self,pose,now):
        if self.last_pose and math.hypot(pose.x-self.last_pose.x,pose.y-self.last_pose.y)>20:
            self.reset()
            return False,0.,"Смена позиции: повторное определение"
        self.last_pose=pose
        self.stable+=1
        if self.stable<4:
            return False,0.,"Подтверждение позиции"
        p=np.array([pose.x,pose.y])
        if self.target is None:
            candidates=[]
            for i,start in enumerate(self.route):
                end=self.route[(i+1)%len(self.route)]
                vec=end-start
                t=float(np.clip(np.dot(p-start,vec)/max(np.dot(vec,vec),.001),0,1))
                candidates.append((np.linalg.norm(p-(start+t*vec)),(i+1)%len(self.route)))
            distance,self.target=min(candidates)
            if distance>self.join_distance:
                self.target=None
                return False,0.,"Игрок вне маршрута — подойди к линии"
            self.progress_pos,self.progress_time=p,now
        goal=self.route[self.target]
        if np.linalg.norm(goal-p)<=self.arrival:
            self.target=(self.target+1)%len(self.route)
            goal=self.route[self.target]
        error=wrap(math.atan2(goal[1]-p[1],goal[0]-p[0])-pose.heading)
        walking=abs(error)<math.radians(22)
        if np.linalg.norm(p-self.progress_pos)>=2:
            self.progress_pos,self.progress_time=p,now
        if not walking:
            self.progress_time=now
        if now-self.progress_time>3:
            raise RuntimeError("Нет продвижения 3 секунды: остановка.")
        return walking,error,f"Точка {self.target+1}/{len(self.route)}"

class PlayerMarkerVision:
    """Detect the bright player disc plus its adjacent white direction wedge."""
    def locate(self,bgr):
        hsv=cv2.cvtColor(bgr,cv2.COLOR_BGR2HSV)
        yellow=cv2.inRange(hsv,np.array([15,125,205],np.uint8),np.array([40,255,255],np.uint8))
        blue=cv2.inRange(hsv,np.array([85,100,205],np.uint8),np.array([125,255,255],np.uint8))
        body=cv2.bitwise_or(yellow,blue)
        white=cv2.inRange(hsv,np.array([0,0,195],np.uint8),np.array([179,75,255],np.uint8))
        n,labels,stats,centers=cv2.connectedComponentsWithStats(body)
        wn,wl,ws,wc=cv2.connectedComponentsWithStats(white)
        candidates=[]
        for i in range(1,n):
            x,y,w,h,area=stats[i]
            if not (35<=area<=360 and 6<=w<=25 and 6<=h<=25 and .65<w/h<1.55 and area/(w*h)>.57): continue
            cx,cy=centers[i]
            radius=(w+h)/4
            for j in range(1,wn):
                wx,wy,ww,wh,wa=ws[j]
                if not 8<=wa<=130 or ww>22 or wh>22: continue
                px,py=wc[j]
                distance=math.hypot(px-cx,py-cy)
                if not radius*.8<=distance<=radius*2.1: continue
                if abs(px-cx)>w+8 or abs(py-cy)>h+8: continue
                angle=math.atan2(py-cy,px-cx)
                candidates.append(Pose(float(cx),float(cy),angle,min(1.,area/(w*h))))
        if len(candidates)!=1:
            return None,"Маркер не найден" if not candidates else "Несколько возможных маркеров — остановка"
        return candidates[0],"Маркер и направление определены"
