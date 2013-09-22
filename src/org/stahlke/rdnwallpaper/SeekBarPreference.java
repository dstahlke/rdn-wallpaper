// This was based upon code from
//   http://robobunny.com/wp/2011/08/13/android-seekbar-preference
// but has now been mutated beyond recognition.
package org.stahlke.rdnwallpaper;

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
import android.widget.RelativeLayout;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;

public class SeekBarPreference extends Preference {
    private final String TAG = getClass().getName();

    private static final String ANDROIDNS="http://schemas.android.com/apk/res/android";
    private static final String RDNWALLPAPERNS="http://www.stahlke.org/dan/rdnwallpaper";
    private static final float DEFAULT_VALUE = 50;

    private float mMaxValue = 100;
    private float mMinValue = 0;
    private float mStepValue = 1;
    private float mCurrentValue;
    private String mFormat;
    private TheView mSeekBar;

    class TheView extends View {
        private Paint mTextPaint;
        private int mAscent;
        private float mLastTouchX;

        public TheView(Context context, AttributeSet attrs) {
            super(context, attrs);
            init();
        }

        private final void init() {
            mTextPaint = new Paint();
            mTextPaint.setAntiAlias(true);
            mTextPaint.setTextSize(32);
            mTextPaint.setColor(0xFF00FF00);
            setPadding(3, 3, 3, 3);
        }

        /**
         * @see android.view.View#measure(int, int)
         */
        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            setMeasuredDimension(measureWidth(widthMeasureSpec),
                    measureHeight(heightMeasureSpec));
        }

        /**
         * Determines the width of this view
         * @param measureSpec A measureSpec packed into an int
         * @return The width of the view, honoring constraints from measureSpec
         */
        private int measureWidth(int measureSpec) {
            return MeasureSpec.getSize(measureSpec);
        }

        /**
         * Determines the height of this view
         * @param measureSpec A measureSpec packed into an int
         * @return The height of the view, honoring constraints from measureSpec
         */
        private int measureHeight(int measureSpec) {
            int result = 0;
            int specMode = MeasureSpec.getMode(measureSpec);
            int specSize = MeasureSpec.getSize(measureSpec);

            mAscent = (int) mTextPaint.ascent();
            if (specMode == MeasureSpec.EXACTLY) {
                // We were told how big to be
                result = specSize;
            } else {
                // Measure the text (beware: ascent is a negative number)
                result = (int) (-mAscent + mTextPaint.descent()) + getPaddingTop()
                        + getPaddingBottom();
                if (specMode == MeasureSpec.AT_MOST) {
                    // Respect AT_MOST value if that was what is called for by measureSpec
                    result = Math.min(result, specSize);
                }
            }
            return result;
        }

        @Override
        public boolean onTouchEvent(MotionEvent ev) {
            int action = ev.getActionMasked();
            Log.i(RdnWallpaper.TAG, "touch "+action+","+ev.getX()+","+ev.getY());

            // Try and prevent focus loss during drag.  For some reason, this is not enough to
            // prevent focus loss when notifyChanged() is called, so we will only do that upon
            // ACTION_UP.
            switch(action) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_MOVE:
                    if(getParent() != null) {
                        getParent().requestDisallowInterceptTouchEvent(true);
                    }
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    if(getParent() != null) {
                        getParent().requestDisallowInterceptTouchEvent(false);
                    }
                    break;
            }

            float newValue = mCurrentValue;

            if(action == MotionEvent.ACTION_DOWN) {
                mLastTouchX = ev.getX();
            } else if(
                action == MotionEvent.ACTION_MOVE ||
                action == MotionEvent.ACTION_UP
            ) {
                float delta = ev.getX() - mLastTouchX;
                mLastTouchX = ev.getX();

                Log.i(RdnWallpaper.TAG, "step="+delta+"*"+mStepValue);
                newValue += delta * mStepValue;

                if(newValue > mMaxValue)
                    newValue = mMaxValue;
                else if(newValue < mMinValue)
                    newValue = mMinValue;
            }

            if(newValue != mCurrentValue) {
                if(!callChangeListener(newValue)) {
                    // change rejected, revert to the previous value
                    newValue = mCurrentValue;
                } else {
                    // change accepted, store it
                    setValue(newValue);
                }
            }

            if(action == MotionEvent.ACTION_UP) {
                // Note: calling this during drag causes focus loss.  Update events seem to
                // be sent even without calling this though.
                notifyChanged();
            }

            return true;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            String text = String.format(mFormat, mCurrentValue);
            canvas.drawText(text, getPaddingLeft(), getPaddingTop() - mAscent,
                    mTextPaint);
        }
    }

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
        mSeekBar = new TheView(context, attrs);
    }

    private void setValuesFromXml(AttributeSet attrs) {
        mMinValue = attrs.getAttributeFloatValue(RDNWALLPAPERNS, "min", 0);
        mMaxValue = attrs.getAttributeFloatValue(RDNWALLPAPERNS, "max", 100);
        mStepValue = attrs.getAttributeFloatValue(RDNWALLPAPERNS, "rate",
                (mMaxValue - mMinValue) / 1000f);

        String summary = attrs.getAttributeValue(ANDROIDNS, "summary");
        if(summary == null) summary = "";

        mFormat = attrs.getAttributeValue(RDNWALLPAPERNS, "format");
        if(mFormat == null) mFormat = summary+": %g";
    }

    @Override
    protected View onCreateView(ViewGroup parent){
        View layout =  null;

        try {
            LayoutInflater mInflater = (LayoutInflater)
                getContext().getSystemService(Context.LAYOUT_INFLATER_SERVICE);

            layout = mInflater.inflate(
                R.layout.seek_bar_preference, parent, false);
        } catch(Exception e) {
            Log.e(TAG, "Error creating seek bar preference", e);
        }

        return layout;

    }

    @Override
    public void onBindView(View view) {
        super.onBindView(view);

        try {
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
        } catch(Exception ex) {
            Log.e(TAG, "Error binding view: " + ex.toString());
        }
    }

    // Call this manually to update value when pref is changed programatically.
    public void setValue(float x) {
        mCurrentValue = x;
        persistFloat(mCurrentValue);
        Log.i(RdnWallpaper.TAG, "setValue "+mCurrentValue);
        // redraw
        if(mSeekBar != null) mSeekBar.invalidate();
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
        } else {
            float temp = 0;
            try {
                temp = (Float)defaultValue;
            } catch(Exception ex) {
                Log.e(TAG, "Invalid default value: " + defaultValue.toString());
            }

            persistFloat(temp);
            mCurrentValue = temp;
        }
    }
}
