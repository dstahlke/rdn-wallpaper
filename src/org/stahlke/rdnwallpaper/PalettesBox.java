package org.stahlke.rdnwallpaper;

import java.util.List;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.content.Context;
import android.content.res.TypedArray;
import android.preference.Preference;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.*;

public class PalettesBox extends Preference {
    private final String TAG = getClass().getName();

    private static final String ANDROIDNS="http://schemas.android.com/apk/res/android";

    private Context mContext;
    private LinearLayout mButtonsBox;
    private int mFnId;
    private SharedPreferences mPrefs;

    public PalettesBox(Context context, AttributeSet attrs) {
        super(context, attrs);
        initPreference(context, attrs);
    }

    public PalettesBox(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        initPreference(context, attrs);
    }

    private void initPreference(Context context, AttributeSet attrs) {
        mContext = context;
        mButtonsBox = new LinearLayout(context, attrs);

        mPrefs = PreferenceManager
                .getDefaultSharedPreferences(context);

        setFunction(0);
    }

    public void setFunction(int f_id) {
        mFnId = f_id;
        int pal = mPrefs.getInt("palette"+mFnId, 0);

        String[] labels = mContext.getResources().getStringArray(
                mContext.getResources().getIdentifier(
                    "palettes"+mFnId, "array", mContext.getPackageName()));

        mButtonsBox.removeAllViews();
        RadioGroup rg = new RadioGroup(mContext);
        rg.setOrientation(RadioGroup.HORIZONTAL);
        for(int i=0; i<labels.length; i++) {
            RadioButton b = new RadioButton(mContext);

            final int ii = i;
            b.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    buttonClicked(ii);
                }
            });

            b.setText(labels[i]);
            rg.addView(b);

            b.setChecked(i==pal);
        }

        mButtonsBox.addView(rg);
    }

    protected void buttonClicked(int i) {
        Log.i(TAG, "palette button clicked: "+i);
        SharedPreferences.Editor ed = mPrefs.edit();
        ed.putInt("palette"+mFnId, i);
        ed.apply();
    }

    @Override
    protected View onCreateView(ViewGroup parent){
        View layout =  null;

        try {
            LayoutInflater mInflater = (LayoutInflater)
                getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);

            layout = mInflater.inflate(
                R.layout.pref_buttons_container, parent, false);
        } catch(Exception e) {
            Log.e(TAG, "Error creating palettes box", e);
        }

        return layout;

    }

    @Override
    public void onBindView(View view) {
        super.onBindView(view);

        try {
            // move our container to the new view we've been given
            ViewParent oldContainer = mButtonsBox.getParent();
            ViewGroup newContainer = (ViewGroup)view.findViewById(
                    R.id.PrefButtonsContainer);

            if (oldContainer != newContainer) {
                // remove the container from the old view
                if (oldContainer != null) {
                    ((ViewGroup) oldContainer).removeView(mButtonsBox);
                }
                // remove the existing container (there may not be one) and add ours
                newContainer.removeAllViews();
                newContainer.addView(mButtonsBox,
                        ViewGroup.LayoutParams.FILL_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
            }
        } catch(Exception ex) {
            Log.e(TAG, "Error binding view: " + ex.toString());
        }
    }
}
