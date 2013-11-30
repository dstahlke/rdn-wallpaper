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

public class RdnPrefs extends PreferenceActivity implements
    SharedPreferences.OnSharedPreferenceChangeListener
{
    //private ParamsView mView;
    private int mLastFunction = -1;
    private List<List<SeekBarPreference>> sliders;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.prefs);

        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        prefs.registerOnSharedPreferenceChangeListener(this);

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
                    RdnWallpaper.resetGrid();
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

        // This is a hack to trigger the sending of an onSharedPreferenceChanged
        // event, which causes RdnWallpaper to start running again (Android
        // pauses wallpapers when the settings dialog is opened).
        SharedPreferences.Editor ed = prefs.edit();
        ed.putBoolean("foo", !prefs.getBoolean("foo", false));
        ed.apply();
    }

    private void setListDefaultIfZero(String id, int val) {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        if(prefs.getString(id, "0").equals("0")) {
            ListPreference pref = (ListPreference)findPreference(id);
            pref.setValue(""+val);
        }
    }

    public void onSharedPreferenceChanged(SharedPreferences prefs, String key) {
        if(RdnWallpaper.DEBUG) Log.i(RdnWallpaper.TAG, "RdnPrefs.onSharedPreferenceChanged");
        updateFunction(true);

        setListTitleToVal("function");
        setListSummaryToVal("resolution");
        setListSummaryToVal("repeatX");
        setListSummaryToVal("repeatY");
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
            RdnWallpaper.resetGrid();
        }
    }
}
