// Adapted from http://robobunny.com/wp/2011/08/13/android-seekbar-preference
package org.stahlke.rdnwallpaper;

import android.content.Context;
import android.content.res.TypedArray;
import android.preference.Preference;
import android.util.AttributeSet;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;

public class SeekBarPreference extends Preference implements OnSeekBarChangeListener {

    private final String TAG = getClass().getName();

    private static final String ANDROIDNS="http://schemas.android.com/apk/res/android";
    private static final String ROBOBUNNYNS="http://robobunny.com";
    private static final float DEFAULT_VALUE = 50;

    private int mResolution    = 1000;
    private float mMaxValue      = 100;
    private float mMinValue      = 0;
    private float mCurrentValue;
    private String mUnitsLeft  = "";
    private String mUnitsRight = "";
    private SeekBar mSeekBar;

    private TextView mStatusText;

    public SeekBarPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
        initPreference(context, attrs);
    }

    public SeekBarPreference(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
        initPreference(context, attrs);
    }

    private void initPreference(Context context, AttributeSet attrs) {
        setValuesFromXml(attrs);
        mSeekBar = new SeekBar(context, attrs);
        mSeekBar.setMax(mResolution);
        mSeekBar.setOnSeekBarChangeListener(this);
    }

    private void setValuesFromXml(AttributeSet attrs) {
        mMaxValue = attrs.getAttributeFloatValue(ROBOBUNNYNS, "max", 100);
        mMinValue = attrs.getAttributeFloatValue(ROBOBUNNYNS, "min", 0);

        mUnitsLeft = getAttributeStringValue(attrs, ROBOBUNNYNS, "unitsLeft", "");
        String units = getAttributeStringValue(attrs, ROBOBUNNYNS, "units", "");
        mUnitsRight = getAttributeStringValue(attrs, ROBOBUNNYNS, "unitsRight", units);
    }

    private String getAttributeStringValue(AttributeSet attrs, String namespace, String name, String defaultValue) {
        String value = attrs.getAttributeValue(namespace, name);
        if(value == null)
            value = defaultValue;

        return value;
    }

    @Override
    protected View onCreateView(ViewGroup parent){

        RelativeLayout layout =  null;

        try {
            LayoutInflater mInflater = (LayoutInflater) getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);

            layout = (RelativeLayout)mInflater.inflate(R.layout.seek_bar_preference, parent, false);
        }
        catch(Exception e)
        {
            Log.e(TAG, "Error creating seek bar preference", e);
        }

        return layout;

    }

    @Override
    public void onBindView(View view) {
        super.onBindView(view);

        try
        {
            // move our seekbar to the new view we've been given
            ViewParent oldContainer = mSeekBar.getParent();
            ViewGroup newContainer = (ViewGroup) view.findViewById(R.id.seekBarPrefBarContainer);

            if (oldContainer != newContainer) {
                // remove the seekbar from the old view
                if (oldContainer != null) {
                    ((ViewGroup) oldContainer).removeView(mSeekBar);
                }
                // remove the existing seekbar (there may not be one) and add ours
                newContainer.removeAllViews();
                newContainer.addView(mSeekBar, ViewGroup.LayoutParams.FILL_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
            }
        }
        catch(Exception ex) {
            Log.e(TAG, "Error binding view: " + ex.toString());
        }

        updateView(view);
    }

    /**
     * Update a SeekBarPreference view with our current state
     * @param view
     */
    protected void updateView(View view) {

        try {
            RelativeLayout layout = (RelativeLayout)view;

            mStatusText = (TextView)layout.findViewById(R.id.seekBarPrefValue);
            mStatusText.setText(String.valueOf(mCurrentValue));
            mStatusText.setMinimumWidth(30);

            mSeekBar.setProgress(encodeValue(mCurrentValue));

            TextView unitsRight = (TextView)layout.findViewById(R.id.seekBarPrefUnitsRight);
            unitsRight.setText(mUnitsRight);

            TextView unitsLeft = (TextView)layout.findViewById(R.id.seekBarPrefUnitsLeft);
            unitsLeft.setText(mUnitsLeft);

        }
        catch(Exception e) {
            Log.e(TAG, "Error updating seek bar preference", e);
        }

    }

    private float decodeValue(int x) {
        float y = (mMaxValue - mMinValue) * x / mResolution + mMinValue;
        return Math.min(Math.max(y, mMinValue), mMaxValue);
    }

    private int encodeValue(float x) {
        return (int)((x - mMinValue) / (mMaxValue - mMinValue) * mResolution);
    }

    // Call this manually to update value when pref is changed programatically.
    // FIXME - any way to do this automatically?
    public void setValue(float x) {
        mCurrentValue = x;
        mSeekBar.setProgress(encodeValue(mCurrentValue));
        // FIXME - need to call persistFloat?
    }

    public void setRange(float min, float max) {
        mMinValue = min;
        mMaxValue = max;
        mSeekBar.setProgress(encodeValue(mCurrentValue));
    }

    @Override
    public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
        float newValue = decodeValue(progress);

        if(newValue > mMaxValue)
            newValue = mMaxValue;
        else if(newValue < mMinValue)
            newValue = mMinValue;

        if(!callChangeListener(newValue)) {
            // change rejected, revert to the previous value
            seekBar.setProgress(encodeValue(mCurrentValue));
            return;
        }

        // change accepted, store it
        mCurrentValue = newValue;
        if(mStatusText != null) {
            mStatusText.setText(String.valueOf(newValue));
        }
        persistFloat(newValue);

    }

    @Override
    public void onStartTrackingTouch(SeekBar seekBar) {}

    @Override
    public void onStopTrackingTouch(SeekBar seekBar) {
        notifyChanged();
    }

    @Override
    protected Object onGetDefaultValue(TypedArray ta, int index){
        float defaultValue = ta.getFloat(index, DEFAULT_VALUE);
        return defaultValue;
    }

    @Override
    protected void onSetInitialValue(boolean restoreValue, Object defaultValue) {
        if(restoreValue) {
            mCurrentValue = getPersistedFloat(mCurrentValue);
        }
        else {
            float temp = 0;
            try {
                temp = (Float)defaultValue;
            }
            catch(Exception ex) {
                Log.e(TAG, "Invalid default value: " + defaultValue.toString());
            }

            persistFloat(temp);
            mCurrentValue = temp;
        }

    }

}

