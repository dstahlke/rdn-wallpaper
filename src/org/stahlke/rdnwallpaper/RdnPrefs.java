package org.stahlke.rdnwallpaper;

import java.util.*;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.preference.*;
import android.widget.Button;
import android.widget.LinearLayout;
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
                Log.i(RdnWallpaper.TAG, "pref "+pref_label+" = "+slider);
                if(slider == null) break;
                fn_sliders.add(slider);
            }
            sliders.add(fn_sliders);
        }

        // update dialog to reflect currently selected function
        updateFunction(false);

        // This is a hack to trigger the sending of an onSharedPreferenceChanged
        // event, which causes RdnWallpaper to start running again (Android
        // pauses wallpapers when the settings dialog is opened).
        SharedPreferences.Editor ed = prefs.edit();
        ed.putBoolean("foo", !prefs.getBoolean("foo", false));
        ed.apply();
    }

    public void reseedPressed(View view) {
        Log.i(RdnWallpaper.TAG, "reseedPressed");
        RdnWallpaper.resetGrid();
    }

    public void onSharedPreferenceChanged(SharedPreferences prefs, String key) {
        Log.i(RdnWallpaper.TAG, "RdnPrefs.onSharedPreferenceChanged");
        updateFunction(true);
    }

    private void updateFunction(boolean allow_reset_grid) {
        SharedPreferences prefs = PreferenceManager
                .getDefaultSharedPreferences(this);
        int f_id = Integer.parseInt(prefs.getString("function", "0"));
        if(f_id == mLastFunction) return;
        mLastFunction = f_id;
        Log.i(RdnWallpaper.TAG, "function id="+f_id);

        ListPreference fn_pref = (ListPreference)findPreference("function");
        fn_pref.setSummary(fn_pref.getEntry());

        PreferenceCategory sliders_box = (PreferenceCategory)findPreference("slider_params");
        sliders_box.removeAll();
        for(SeekBarPreference slider : sliders.get(f_id)) {
            sliders_box.addPreference(slider);
        }

        PresetsBox presets_box = (PresetsBox)findPreference("presets_box");
        presets_box.setFunction(f_id);
        presets_box.setSliders(sliders.get(f_id));

        if(allow_reset_grid) {
            RdnWallpaper.resetGrid();
        }
    }
}
