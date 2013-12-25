// This module is a hack that allows the wallpaper to be not paused when the
// settings window is opened.  It seems needlessly complicated, but there are
// many cases to take care of:
// * Settings opened directly from wallpaper menu (this option is available in
//   Gingerbread, or at least CyanogenMod)
// * Settings opened from wallpaper chooser preview, when this wallpaper is also
//   already active
// * Settings opened from wallpaper chooser preview, when this wallpaper is not
//   already active
// * Screen turned on/off during each of those cases
// There seems to be no way to directly detect which of these is happening.  So
// it is hacks upon hacks.  It seems to work okay.  When screen power is toggled
// on the wallpaper preview screen (not in the settings screen) and the
// wallpaper is already active on the desktop, it seems that two different
// engines can end up being isVisible at once when the lockscreen is visible.
// But this doesn't seem to have adverse effect.

package org.stahlke.rdnwallpaper;

import java.util.*;
import android.util.Log;

class RecentWaker {
    private List<RdnWallpaper.MyEngine> mEngines = new ArrayList<RdnWallpaper.MyEngine>();
    private Map<RdnWallpaper.MyEngine, Boolean> mVisMap =
        new HashMap<RdnWallpaper.MyEngine, Boolean>();
    private RdnWallpaper.MyEngine freeze_on_pause = null;
    private boolean wake_e2_on_e1_freeze = false;
    private boolean ignore_event = false;

    synchronized public void add(RdnWallpaper.MyEngine t) {
        mEngines.add(t);
        freeze_on_pause = null;
        wake_e2_on_e1_freeze = false;
        mVisMap.put(t, false);
    }

    synchronized public void remove(RdnWallpaper.MyEngine t) {
        mEngines.remove(t);
        freeze_on_pause = null;
        wake_e2_on_e1_freeze = false;
        mVisMap.remove(t);
    }

    synchronized public void onEngineVisibilityChanged(
        RdnWallpaper.MyEngine eng, boolean visible
    ) {
        mVisMap.put(eng, visible);
        if(ignore_event) return;

        if(RdnWallpaper.DEBUG) {
            Log.i(RdnWallpaper.TAG, "waker: onVis("+eng+","+visible+") size="+
                    mEngines.size());
            for(RdnWallpaper.MyEngine e : mEngines) {
                Log.i(RdnWallpaper.TAG, "waker:     "+e+","+mVisMap.get(e));
            }
        }
        if(mEngines.size() == 1 && visible && freeze_on_pause != null) {
            if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG,
                    "waker: engine woke itself up; clearing freeze_on_pause");
            freeze_on_pause = null;
        }
        if(mEngines.size() == 2) {
            RdnWallpaper.MyEngine wall = mEngines.get(0); // wallpaper
            RdnWallpaper.MyEngine prev = mEngines.get(1); // preview
            if(eng == wall) {
                if(visible) {
                    if(mVisMap.get(prev)) {
                        wake_e2_on_e1_freeze = true;
                        if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG,
                                "waker: freezing "+prev+"; will wake prev on wall freeze");
                        ignore_event = true;
                        prev.onVisibilityChanged(false);
                        ignore_event = false;
                    }
                } else if(wake_e2_on_e1_freeze) {
                    if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "waker: waking "+prev);
                    ignore_event = true;
                    prev.onVisibilityChanged(true);
                    ignore_event = false;
                    wake_e2_on_e1_freeze = false;
                }
            }
        }
    }

    synchronized public void onPrefsVisibilityChanged(boolean visible) {
        if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "waker: onPrefVis("+visible+")");
        if(visible) {
            if(mEngines.size() == 0) return;
            RdnWallpaper.MyEngine last_eng = mEngines.get(mEngines.size()-1);
            if(!mVisMap.get(last_eng)) {
                freeze_on_pause = last_eng;
                if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG,
                        "waker: waking "+last_eng+"; will freeze it when prefs stop");
                ignore_event = true;
                last_eng.onVisibilityChanged(true);
                ignore_event = false;
            }
        } else {
            if(freeze_on_pause != null) {
                if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG,
                        "waker: freezing "+freeze_on_pause+" because prefs stopped");
                ignore_event = true;
                freeze_on_pause.onVisibilityChanged(false);
                ignore_event = false;
                freeze_on_pause = null;
            }
        }
    }
}
