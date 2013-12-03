#!/usr/bin/python

# This can be used to prototype before writing a reaction-diffusion system up in C.
# For some reason it's really slow.

import numpy as np
import numpy.random as random
import scipy.ndimage.filters
import gobject
import matplotlib
matplotlib.use('GTKAgg')
import matplotlib.pyplot as plt
import matplotlib.cm as cm

class GrayScott(object):
    def __init__(self):
        self.D = 0.1
        self.F = 0.01
        self.k = 0.049
        self.dt = 1

        self.bg_val = np.array([ 1, 0 ])
        self.seed_vals = [
            np.array([
                ((i * 5) %  7) / 7.0,
                ((i * 9) % 13) / 13.0
            ]) for i in range(100) ]

        self.diffusion = self.D * np.diag([ 2, 1 ])

    def get_dx_dt(self, grid):
        (a, b) = grid
        da = -a*b*b + self.F*(1.0-a)
        db =  a*b*b - (self.F+self.k)*b
        return np.array([da, db])

class GinzburgLandau(object):
    def __init__(self):
        self.D = 0.2
        self.alpha = 0.3
        self.beta = 2.0
        self.dt = 0.005
        self.n = 3

        self.bg_val = np.array([ 1, 0, 0 ])
        self.seed_vals = [
            self.bg_val + np.random.standard_normal(self.n)
            for i in range(100) ]

        self.diffusion = self.D * (np.eye(self.n) + self.alpha*self.antisym())
        self.nonlin = np.eye(self.n) + self.beta*self.antisym()

    def antisym(self):
        ret = np.random.standard_normal((self.n, self.n))
        return ret - ret.T

    def get_dx_dt(self, grid):
        r2 = np.sum(np.abs(grid)**2, axis=0)
        R = np.tensordot(self.nonlin, grid, axes=([1], [0]))
        return grid - R * np.tensordot(np.ones(self.n), r2, axes=0)

class RaysModel(object):
    def __init__(self):
        self.D = 3.0
        self.dt = 0.0001
        self.eps = 0.1
        self.sigma = 0.04
        self.gamma = 1.5
        self.tau = 0.01
        self.k1 = 1.0
        self.k2 = 5.0
        self.h1 = 1.0
        self.h2 = 0.8

        Lu = self.eps / self.sigma
        Lv = 1.0
        Lw = 7.0 / self.tau

        self.bg_val = np.array([ 0.9, 0.1, 0.1 ])
        self.seed_vals = [
            self.bg_val + 5.0*np.array([
                ((i * 5) %  7) / 7.0,
                ((i * 9) % 13) / 13.0,
                ((i * 3) %  5) / 5.0
            ]) for i in range(100) ]

        self.diffusion = self.D * np.diag([ Lu, Lv, Lw ])

    def get_dx_dt(self, grid):
        (u, v, w) = grid
        du = (1.0/(self.sigma*self.eps)) * (self.gamma*u - u*u*u - self.k1*v - self.k2*w)
        dv = u - v + self.h1
        dw = (1.0/self.tau) * (u - w - self.h2)
        return np.array([du, dv, dw])

random.seed(1)
fn = GinzburgLandau()

(w, h) = (200, 200)
N = fn.bg_val.shape[0]
grid = np.tensordot(fn.bg_val, np.ones((w, h)), axes=0)

for seed_idx in range(100):
    s = 20
    x0 = np.random.randint(w-s)
    y0 = np.random.randint(w-s)
    for x in range(s):
        for y in range(s):
            grid[:,x0+x,y0+y] = fn.seed_vals[seed_idx]

fig = plt.figure()
ax = fig.add_subplot(111)

#grid = np.zeros((2,10,10))
#grid[0,2,2] = 1
#grid[1,1,2] = -1

def update():
    global grid

    for i in range(5):
        print np.max(grid), str([ "%.2g,%.2g" % (np.mean(x), np.var(x)) for x in grid ])
        L = np.array([ scipy.ndimage.filters.laplace(grid[i,:,:], mode='wrap') for i in range(N) ])
        #print grid.shape, fn.diffusion.shape, L.shape
        grid += fn.dt * np.tensordot(fn.diffusion, L, axes=([1], [0]))
        grid += fn.dt * fn.get_dx_dt(grid)

    #img = grid[1,:,:]
    #img = np.rollaxis(grid, 0, 3)
    img = np.sum(grid**2, axis=0)

    ax.cla()
    ax.imshow(img, cmap=cm.gray, interpolation='nearest')
    fig.canvas.draw()
    return True

gobject.idle_add(update)

plt.show()
