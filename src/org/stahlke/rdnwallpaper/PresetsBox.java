package org.stahlke.rdnwallpaper;

import java.util.List;

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

public class PresetsBox extends Preference {
    private final String TAG = getClass().getName();

    private static final String ANDROIDNS="http://schemas.android.com/apk/res/android";

    private Context mContext;
    private LinearLayout mButtonsBox;
    private int mFnId;
    private List<SeekBarPreference> mSliders;

    public PresetsBox(Context context, AttributeSet attrs) {
        super(context, attrs);
        initPreference(context, attrs);
    }

    public PresetsBox(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        initPreference(context, attrs);
    }

    private void initPreference(Context context, AttributeSet attrs) {
        mContext = context;
        mButtonsBox = new LinearLayout(context, attrs);
        setFunction(0);
    }

    public void setFunction(int f_id) {
        mFnId = f_id;

        String[] preset_labels = mContext.getResources().getStringArray(
                mContext.getResources().getIdentifier(
                    "presets"+mFnId, "array", mContext.getPackageName()));

        mButtonsBox.removeAllViews();
        for(int i=0; i<preset_labels.length; i++) {
            Button b = new Button(mContext);
            final int ii = i;
            b.setOnClickListener(new View.OnClickListener() {
                public void onClick(View v) {
                    presetClicked(ii);
                }
            });
            b.setText(preset_labels[i]);
            mButtonsBox.addView(b);
        }
    }

    public void setSliders(List<SeekBarPreference> sliders) {
        mSliders = sliders;
    }

    protected void presetClicked(int i) {
        Log.i(TAG, "presetClicked "+i);

        TypedArray preset_vals = mContext.getResources().obtainTypedArray(
                mContext.getResources().getIdentifier(
                    "presets"+mFnId+"_"+i, "array", mContext.getPackageName()));

        for(int j=0; j<mSliders.size(); j++) {
            float val = preset_vals.getFloat(j, 0);
            Log.i(TAG, "slider["+j+"]="+val);
            mSliders.get(j).setValue(val);
        }

        RdnWallpaper.resetGrid();
    }

    @Override
    protected View onCreateView(ViewGroup parent){
        View layout =  null;

        try {
            LayoutInflater mInflater = (LayoutInflater)
                getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);

            layout = mInflater.inflate(
                R.layout.preset_buttons, parent, false);
        } catch(Exception e) {
            Log.e(TAG, "Error creating presets box", e);
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
                    R.id.PresetButtonsContainer);

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
