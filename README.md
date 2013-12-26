A live wallpaper of reaction-diffusion patterns.

To build:

    hg clone https://bitbucket.org/erublee/eigen-android
    ndk-build
    ant debug

Currently, Ginzburg-Landau and Gray-Scott are implemented.  Let me know if you know of any
other reactions that create nice dynamic patterns in 2D.  FitzHugh-Nagumo is a possibility, but
it seems this needs to be coupled to another system, which may be slow.

![Screenshot 1](ss_gs.png)
![Screenshot 1](ss_gl.png)
