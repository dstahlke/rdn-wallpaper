package org.stahlke.rdnwallpaper;

import java.util.*;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.preference.*;
import android.graphics.*;
import android.content.Context;
import android.view.*;
import android.os.Bundle;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceActivity;
import android.widget.Toast;
import android.util.Log;
import android.content.res.TypedArray;

public class RdnPrefs extends PreferenceActivity implements
    SharedPreferences.OnSharedPreferenceChangeListener
{
    //private ParamsView mView;
    private int mLastFunction = -1;
    private List<List<SeekBarPreference>> sliders;
    private SeekBarPreference mHueSlider;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.prefs);

        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        prefs.registerOnSharedPreferenceChangeListener(this);

        // Get a ref to this now, because its key will change in setHueKey.
        mHueSlider = (SeekBarPreference)findPreference("hue");

        sliders = new ArrayList<List<SeekBarPreference>>();
        for(int i=0; ; i++) {
            if(findPreference("param_"+i+"_0") == null) break;

            List<SeekBarPreference> fn_sliders = new ArrayList<SeekBarPreference>();
            for(int j=0; ; j++) {
                String pref_label = "param_"+i+"_"+j;
                SeekBarPreference slider = (SeekBarPreference)findPreference(pref_label);
                if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "pref "+pref_label+" = "+slider);
                if(slider == null) break;
                fn_sliders.add(slider);
            }
            sliders.add(fn_sliders);
        }

        Preference button = (Preference)findPreference("reseed_button");
        button.setOnPreferenceClickListener(
            new Preference.OnPreferenceClickListener() {
                @Override
                public boolean onPreferenceClick(Preference arg0) {
                    if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "reseedPressed");
                    RdnRenderer.resetGrid();
                    return true;
                }
        });

        // update dialog to reflect currently selected function
        updateFunction(false);

        Display display = getWindowManager().getDefaultDisplay();
        int w = display.getWidth();
        int h = display.getHeight();
        setListDefaultIfZero("resolution", RdnWallpaper.getDefaultRes(w, h));
        setListDefaultIfZero("repeatX", RdnWallpaper.getDefaultRepeatX(w, h));
        setListDefaultIfZero("repeatY", RdnWallpaper.getDefaultRepeatY(w, h));

        setHueKey();

        // This is a hack to make the animation start running again (Android
        // pauses wallpapers when the settings dialog is opened).
        RdnWallpaper.mRecentWaker.wakeupLatest();
    }

    private void setListDefaultIfZero(String id, int val) {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        if(prefs.getString(id, "0").equals("0")) {
            ListPreference pref = (ListPreference)findPreference(id);
            pref.setValue(""+val);
            // Without this, the labels sometimes don't show.
            pref.setSummary("%s");
        }
    }

    public void onSharedPreferenceChanged(SharedPreferences prefs, String key) {
        if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "RdnPrefs.onSharedPreferenceChanged: "+key);
        updateFunction(true);

        setListTitleToVal("function");
        setListSummaryToVal("resolution");
        setListSummaryToVal("repeatX");
        setListSummaryToVal("repeatY");

        if(key.equals("function") || key.startsWith("palette")) {
            setHueKey();
        }
    }

    public static float getHueVal(Context ctx) {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(ctx);
        int f_id = Integer.parseInt(prefs.getString("function", "0"));
        int pal = prefs.getInt("palette"+f_id, 0);
        String key = "hue"+f_id+"_"+pal;

        TypedArray preset_vals = ctx.getResources().obtainTypedArray(
                ctx.getResources().getIdentifier(
                    "default_hue_"+f_id, "array", ctx.getPackageName()));
        float default_val = preset_vals.getFloat(pal, 0);

        return prefs.getFloat(key, default_val);
    }

    private void setHueKey() {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        int f_id = Integer.parseInt(prefs.getString("function", "0"));
        int pal = prefs.getInt("palette"+f_id, 0);
        String key = "hue"+f_id+"_"+pal;

        float hue = getHueVal(getApplicationContext());

        mHueSlider.setKey(key);
        mHueSlider.setValue(hue);
    }

    private void setListTitleToVal(String id) {
        ListPreference pref = (ListPreference)findPreference(id);
        pref.setTitle(pref.getEntry());
    }

    private void setListSummaryToVal(String id) {
        ListPreference pref = (ListPreference)findPreference(id);
        pref.setSummary(pref.getEntry());
    }

    private void updateFunction(boolean allow_reset_grid) {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        int f_id = Integer.parseInt(prefs.getString("function", "0"));
        if(f_id == mLastFunction) return;
        mLastFunction = f_id;
        if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "function id="+f_id);

        PreferenceCategory sliders_box = (PreferenceCategory)findPreference("slider_params");
        sliders_box.removeAll();
        for(SeekBarPreference slider : sliders.get(f_id)) {
            sliders_box.addPreference(slider);
        }

        PresetsBox presets_box = (PresetsBox)findPreference("presets_box");
        presets_box.setFunction(f_id);
        presets_box.setSliders(sliders.get(f_id));

        PalettesBox palettes_box = (PalettesBox)findPreference("palettes_box");
        palettes_box.setFunction(f_id);

        if(allow_reset_grid) {
            RdnRenderer.resetGrid();
        }
    }
}
