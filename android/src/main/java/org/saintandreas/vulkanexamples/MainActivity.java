package org.saintandreas.vulkanexamples;

import android.app.Activity;
import android.app.NativeActivity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import java.lang.annotation.Native;
import java.util.ArrayList;
import java.util.List;


public class MainActivity extends Activity {
    final String TAG = MainActivity.class.getCanonicalName();

    private List<String> mExamples = new ArrayList<>();
    private List<Class<?>> mExamplesClasses = new ArrayList<>();

    private void populateExamples() {
        try {
            PackageManager pm = getApplicationContext().getPackageManager();
            PackageInfo packageInfo = pm.getPackageInfo(getPackageName(), PackageManager.GET_ACTIVITIES);
            for (ActivityInfo activity : packageInfo.activities) {
                Class<?> clazz = Class.forName(activity.name);
                if (NativeActivity.class.isAssignableFrom(clazz)) {
                    mExamples.add(clazz.getSimpleName());
                    mExamplesClasses.add(clazz);
                }
            }
        } catch (PackageManager.NameNotFoundException e) {
            throw new RuntimeException(e);
        } catch (ClassNotFoundException e) {
            throw new RuntimeException(e);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        populateExamples();

        ListView listView = findViewById(R.id.examples_list);
        listView.setAdapter( new ArrayAdapter<>(this, R.layout.simplerow, mExamples) );
        listView.setOnItemClickListener((parent, view, position, id) -> {
            Intent intent = new Intent(getApplicationContext(), mExamplesClasses.get(position));
            startActivity(intent);
        });
    }
}
