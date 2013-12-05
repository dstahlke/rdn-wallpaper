package org.stahlke.rdnwallpaper;

import java.util.*;

class DrawThreadHolder {
    private List<RdnWallpaper.MyEngine> mEngines = new ArrayList<RdnWallpaper.MyEngine>();

    public void add(RdnWallpaper.MyEngine t) {
        mEngines.add(t);
    }

    public void remove(RdnWallpaper.MyEngine t) {
        mEngines.remove(t);
    }

    public void wakeupLatest() {
        RdnWallpaper.MyEngine t = mEngines.get(mEngines.size() - 1);
        t.wakeup();
    }
}
