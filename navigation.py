"""Image-derived path planning. 2-D radar geometry does not encode height."""
import heapq
import math
from collections import deque
import cv2
import numpy as np
from vision import wrap

class GridMap:
    def __init__(self,mask,step=3):
        self.step=step
        self.mask=(mask>0).astype(np.uint8)
        safe=cv2.distanceTransform(self.mask,cv2.DIST_L2,3)>2.
        self.grid=safe[::step,::step]
        self.blocked=set()
    @classmethod
    def from_radar(cls,image):
        # Black voids and brighter crate tops are excluded; editor can refine this mask.
        value=image.max(axis=2)
        mask=((value>=32)&(value<=125)).astype(np.uint8)*255
        mask[:3]=0; mask[-3:]=0; mask[:,:3]=0; mask[:,-3:]=0
        return cls(mask)
    def nearest(self,point,max_distance=12):
        ys,xs=np.nonzero(self.grid)
        if not len(xs): raise RuntimeError("Нет проходимых участков")
        ds=(xs*self.step-point[0])**2+(ys*self.step-point[1])**2
        for k in np.argsort(ds)[:100]:
            node=(int(xs[k]),int(ys[k]))
            if ds[k]>max_distance**2: break
            if node not in self.blocked: return node
        raise RuntimeError("Позиция вне карты проходимости")
    def open(self,p):
        x,y=p
        return 0<=y<self.grid.shape[0] and 0<=x<self.grid.shape[1] and self.grid[y,x] and p not in self.blocked
    def neighbors(self,p):
        x,y=p
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,1),(1,-1),(-1,-1)):
            q=(x+dx,y+dy)
            if not self.open(q): continue
            if dx and dy and not (self.open((x+dx,y)) and self.open((x,y+dy))): continue
            yield q,math.hypot(dx,dy)
    def reachable(self,start):
        seen={start}; pending=deque([start])
        while pending:
            p=pending.popleft()
            for q,_ in self.neighbors(p):
                if q not in seen: seen.add(q); pending.append(q)
        return seen
    def path(self,start,goal):
        heap=[(0.,0.,start)]
        costs={start:0.}; parent={}
        while heap:
            _,cost,p=heapq.heappop(heap)
            if cost!=costs[p]: continue
            if p==goal:
                route=[p]
                while p in parent: p=parent[p]; route.append(p)
                return [(x*self.step,y*self.step) for x,y in reversed(route)]
            for q,edge in self.neighbors(p):
                nc=cost+edge
                if nc<costs.get(q,float("inf")):
                    costs[q]=nc; parent[q]=p
                    heapq.heappush(heap,(nc+math.dist(q,goal),nc,q))
        raise RuntimeError("Путь не найден")
    def visible(self,a,b):
        count=max(2,int(math.dist(a,b)*2))
        for t in np.linspace(0,1,count):
            x,y=np.array(a)+(np.array(b)-a)*t
            if not self.open((int(round(x/self.step)),int(round(y/self.step)))): return False
        return True

class PatrolNavigator:
    def __init__(self,grid):
        self.grid=grid
        self.visited=[]
        self.reset()
    def reset(self):
        self.route=[]
        self.target=0
        self.last_pose=None
        self.progress=None
        self.progress_time=0.
        self.turn_time=None
        self.stable=0
        self.retries=0
    def plan(self,point):
        start=self.grid.nearest(point)
        connected=self.grid.reachable(start)
        if len(connected)<30: raise RuntimeError("Слишком маленькая изолированная область")
        candidates=sorted(connected)
        # Deterministic coverage: distant destinations, with a penalty for recent goals.
        def score(p):
            distance=math.dist(p,start)
            novelty=min((math.dist(p,q) for q in self.visited[-6:]),default=100)
            return distance+1.5*novelty
        goal=max(candidates,key=score)
        self.visited.append(goal)
        self.route=self.grid.path(start,goal)
        self.target=0
        self.progress=np.array(point,float)
        self.progress_time=0.
    def update(self,pose,now):
        p=np.array([pose.x,pose.y])
        if self.last_pose is not None and math.dist(p,self.last_pose)>24:
            self.reset()
            return False,0.,"Возрождение: определяю новый путь"
        self.last_pose=p
        self.stable+=1
        if self.stable<4: return False,0.,"Подтверждение позиции"
        if not self.route:
            self.plan(p); self.progress_time=now
        while self.target<len(self.route)-1 and math.dist(p,self.route[self.target])<3:
            self.target+=1
        if self.target==len(self.route)-1 and math.dist(p,self.route[-1])<4:
            self.plan(p); self.progress_time=now
        # Look ahead only through verified open cells, never through a wall.
        for i in range(self.target+1,min(len(self.route),self.target+6)):
            if math.dist(p,self.route[i])<=12 and self.grid.visible(p,self.route[i]): self.target=i
            else: break
        goal=np.array(self.route[self.target])
        error=wrap(math.atan2(goal[1]-p[1],goal[0]-p[0])-pose.heading)
        walking=abs(error)<math.radians(18)
        if walking:
            self.turn_time=None
            if math.dist(p,self.progress)>=2:
                self.progress=p; self.progress_time=now; self.retries=0
            if now-self.progress_time>3:
                self.retries+=1
                if self.retries>2: raise RuntimeError("Препятствие: повторное планирование не помогло")
                ahead=p+5*np.array([math.cos(pose.heading),math.sin(pose.heading)])
                blocked=self.grid.nearest(ahead)
                self.grid.blocked.add(blocked)
                self.plan(p); self.progress_time=now
                return False,0.,"Препятствие: построен другой путь"
        else:
            self.progress_time=now
            if self.turn_time is None: self.turn_time=now
            elif now-self.turn_time>6: raise RuntimeError("Камера не повернулась к маршруту")
        return walking,error,f"Автомаршрут: {self.target+1}/{len(self.route)}"
