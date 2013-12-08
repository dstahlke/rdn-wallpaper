package org.stahlke.rdnwallpaper;

import java.util.*;

class DrawThreadHolder {
    private List<RdnWallpaper.MyEngine> mEngines = new ArrayList<RdnWallpaper.MyEngine>();

    synchronized public void add(RdnWallpaper.MyEngine t) {
        mEngines.add(t);
    }

    synchronized public void remove(RdnWallpaper.MyEngine t) {
        mEngines.remove(t);
    }

    synchronized public void wakeupLatest() {
        if(mEngines.size() > 0) {
            RdnWallpaper.MyEngine t = mEngines.get(mEngines.size() - 1);
            t.wakeup();
        }
    }
}
