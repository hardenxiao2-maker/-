package com.ti.pingpongctrl;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends Activity {
    /*
     * App 结构说明：
     *   控制页：负责 WiFi/蓝牙连接、RUN/STOP/UP/DOWN/SET。
     *   曲线页：显示 PID 跟踪曲线和风机 RPM 曲线。
     *   参数页：在线填写 PID、PWM、滤波、温度等 CFG 参数。
     *   校准页：在线填写测距校准表 CAL。
     *
     * 参数页和校准页的数据会保存到 SharedPreferences。
     * 每次 WiFi 或蓝牙连接成功后，App 会自动把保存值重新下发给单片机，
     * 从而解决“单片机断电后运行参数恢复默认”的问题。
     */
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int REQ_BT_CONNECT = 31;
    private static final String PREFS_NAME = "pingpong_debug_params";
    private static final String[] CAL_TEMP_OPTIONS = {"15", "20", "25", "30", "35"};

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final Object writeLock = new Object();
    private SharedPreferences prefs;

    private LinearLayout content;
    private Button navControl;
    private Button navChart;
    private Button navParam;
    private Button navCal;

    private EditText ipInput;
    private EditText portInput;
    private EditText btInput;
    private EditText setTargetInput;
    private TextView linkText;
    private TextView stateText;
    private TextView targetText;
    private TextView actualText;
    private TextView pwmText;
    private TextView rpmText;
    private TextView tempText;
    private TextView logText;
    private ChartView chartView;

    private EditText kpInput;
    private EditText kiInput;
    private EditText kdInput;
    private EditText basePwmInput;
    private EditText pwmMinInput;
    private EditText pwmMaxInput;
    private EditText safePwmInput;
    private EditText filterInput;
    private EditText targetMinInput;
    private EditText targetMaxInput;
    private EditText stepInput;
    private EditText tempInput;

    private Spinner calTempSpinner;
    private final EditText[] calMeasuredInputs = new EditText[11];
    private final EditText[] calTrueInputs = new EditText[11];

    private volatile boolean running = false;
    private Socket wifiSocket;
    private BluetoothSocket bluetoothSocket;
    private OutputStream outputStream;
    private Thread readerThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // 把 Window 背景色设成和 App 背景一致，防止手势导航条区域露出黑色窗口背景。
        getWindow().setBackgroundDrawable(
            new android.graphics.drawable.ColorDrawable(Color.rgb(244, 247, 251)));
        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        buildShell();
        showControlPage();
    }

    @Override
    protected void onDestroy() {
        disconnect();
        super.onDestroy();
    }

    private void buildShell() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(248, 249, 250)); // Google Play default background

        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.VERTICAL);
        header.setPadding(dp(20), dp(16), dp(20), dp(10));
        root.addView(header, matchWrap());

        TextView title = new TextView(this);
        title.setText("乒乓球位置控制");
        setTextDp(title, 24);
        title.setIncludeFontPadding(false);
        title.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
        title.setTextColor(Color.rgb(32, 33, 36));
        header.addView(title, matchWrap());

        linkText = new TextView(this);
        linkText.setText("未连接");
        setTextDp(linkText, 13);
        linkText.setIncludeFontPadding(false);
        linkText.setTextColor(Color.rgb(95, 99, 104));
        linkText.setPadding(0, dp(4), 0, 0);
        header.addView(linkText, matchWrap());

        ScrollView scroll = new ScrollView(this);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);
        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(8), dp(4), dp(8), dp(32));
        
        // 启用平滑过渡动画：页面切换时组件会淡入/滑动
        android.animation.LayoutTransition transition = new android.animation.LayoutTransition();
        transition.enableTransitionType(android.animation.LayoutTransition.CHANGING);
        content.setLayoutTransition(transition);
        
        scroll.addView(content);
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout nav = new LinearLayout(this);
        nav.setOrientation(LinearLayout.HORIZONTAL);
        nav.setBackgroundColor(Color.WHITE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            nav.setElevation(dp(16)); // 底部导航栏阴影
        }
        root.addView(nav, matchWrap());

        navControl = navButton("控制", v -> showControlPage());
        navChart = navButton("曲线", v -> showChartPage());
        navParam = navButton("参数", v -> showParamPage());
        navCal = navButton("校准", v -> showCalPage());
        nav.addView(navControl, weight(1f, 40));
        nav.addView(navChart, weight(1f, 40));
        nav.addView(navParam, weight(1f, 40));
        nav.addView(navCal, weight(1f, 40));

        setContentView(root);

        /*
         * 处理系统手势导航条的 Bottom Insets：
         * 把导航条的高度动态加到底部导航栏的 bottom padding 里，
         * 让 nav 的内容在手势条上方正常显示，而不是被摄头或露出黑座。
         */
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
            nav.setOnApplyWindowInsetsListener((v, insets) -> {
                int bottomInset = 0;
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                    bottomInset = insets.getTappableElementInsets().bottom;
                    if (bottomInset == 0) {
                        // 全面屏手势导航模式，使用系统手势条高度
                        bottomInset = insets.getSystemWindowInsetBottom();
                    }
                } else {
                    bottomInset = insets.getSystemWindowInsetBottom();
                }
                v.setPadding(dp(6), dp(6), dp(6), dp(8) + bottomInset);
                return insets;
            });
        } else {
            nav.setPadding(dp(6), dp(6), dp(6), dp(8));
        }
    }

    private void showControlPage() {
        selectNav(navControl);
        content.removeAllViews();

        LinearLayout wifiCard = card();
        content.addView(wifiCard);
        wifiCard.addView(sectionTitle("WiFi TCP"));
        LinearLayout endpoint = row();
        ipInput = edit("192.168.1.1", false);
        portInput = edit("2001", false);
        portInput.setInputType(InputType.TYPE_CLASS_NUMBER);
        endpoint.addView(ipInput, weight(2f, 48));
        endpoint.addView(portInput, weight(1f, 48));
        wifiCard.addView(endpoint);

        LinearLayout wifiBtns = row();
        Button wifiConnect = button("连接 WiFi", Color.rgb(22, 101, 52), Color.WHITE);
        Button disconnect = button("断开", Color.rgb(185, 28, 28), Color.WHITE);
        wifiConnect.setOnClickListener(v -> connectWifi());
        disconnect.setOnClickListener(v -> disconnect());
        wifiBtns.addView(wifiConnect, weight(1f, 46));
        wifiBtns.addView(disconnect, weight(1f, 46));
        wifiCard.addView(wifiBtns);

        LinearLayout btCard = card();
        content.addView(btCard);
        btCard.addView(sectionTitle("蓝牙 SPP"));
        btInput = edit("Makerobo", false);
        btCard.addView(btInput, matchHeight(48));
        Button btConnect = button("连接蓝牙", Color.rgb(37, 99, 235), Color.WHITE);
        btConnect.setOnClickListener(v -> connectBluetooth());
        btCard.addView(btConnect, matchButton());

        LinearLayout grid1 = row();
        stateText = metric(grid1, "状态", "--");
        targetText = metric(grid1, "目标 cm", "--");
        content.addView(grid1);

        LinearLayout grid2 = row();
        actualText = metric(grid2, "实测 cm", "--");
        pwmText = metric(grid2, "PWM", "--");
        content.addView(grid2);

        LinearLayout grid3 = row();
        rpmText = metric(grid3, "转速 RPM", "--");
        tempText = metric(grid3, "温度 ℃", "--");
        content.addView(grid3);

        LinearLayout controlCard = card();
        content.addView(controlCard);
        controlCard.addView(sectionTitle("远程控制"));
        LinearLayout runRow = row();
        Button run = button("启动", Color.rgb(22, 101, 52), Color.WHITE);
        Button stop = button("停止", Color.rgb(185, 28, 28), Color.WHITE);
        run.setOnClickListener(v -> sendCommand("RUN"));
        stop.setOnClickListener(v -> sendCommand("STOP"));
        runRow.addView(run, weight(1f, 48));
        runRow.addView(stop, weight(1f, 48));
        controlCard.addView(runRow);

        LinearLayout stepRow = row();
        Button down = button("- step", Color.WHITE, Color.rgb(31, 41, 55));
        Button up = button("+ step", Color.WHITE, Color.rgb(31, 41, 55));
        down.setOnClickListener(v -> sendCommand("DOWN"));
        up.setOnClickListener(v -> sendCommand("UP"));
        stepRow.addView(down, weight(1f, 46));
        stepRow.addView(up, weight(1f, 46));
        controlCard.addView(stepRow);

        LinearLayout setRow = row();
        setTargetInput = edit("50", true);
        Button set = button("设定目标", Color.WHITE, Color.rgb(31, 41, 55));
        set.setOnClickListener(v -> sendCommand("SET " + value(setTargetInput)));
        setRow.addView(setTargetInput, weight(1f, 46));
        setRow.addView(set, weight(1f, 46));
        controlCard.addView(setRow);

        Button get = button("立即刷新", Color.WHITE, Color.rgb(31, 41, 55));
        get.setOnClickListener(v -> sendCommand("GET"));
        controlCard.addView(get, matchButton());
    }

    private void showChartPage() {
        selectNav(navChart);
        content.removeAllViews();

        chartView = new ChartView(this);
        LinearLayout.LayoutParams lp = matchHeight(430);
        lp.setMargins(0, dp(6), 0, dp(8));
        content.addView(chartView, lp);

        LinearLayout legend = card();
        content.addView(legend);
        legend.addView(smallText("曲线显示最近 180 个采样点，旧数据会从左侧滑出。距离和目标按 cm 显示，PWM/RPM 按各自比例缩放。"));
        Button clear = button("清空曲线", Color.WHITE, Color.rgb(31, 41, 55));
        clear.setOnClickListener(v -> {
            if (chartView != null) chartView.clear();
        });
        legend.addView(clear, matchButton());

        logText = smallText("");
        logText.setTextColor(Color.rgb(226, 232, 240));
        logText.setBackgroundColor(Color.rgb(15, 23, 42));
        logText.setPadding(dp(12), dp(10), dp(12), dp(10));
        LinearLayout.LayoutParams logLp = matchHeight(180);
        logLp.setMargins(0, dp(10), 0, 0);
        content.addView(logText, logLp);
    }

    private void showParamPage() {
        selectNav(navParam);
        content.removeAllViews();

        LinearLayout card = card();
        content.addView(card);
        card.addView(sectionTitle("在线参数调节"));

        kpInput = paramRow(card, "Kp", pref("KP", "22.0"));
        kiInput = paramRow(card, "Ki", pref("KI", "1.5"));
        kdInput = paramRow(card, "Kd", pref("KD", "6.0"));
        basePwmInput = paramRow(card, "base_pwm", pref("BASE_PWM", "350"));
        pwmMinInput = paramRow(card, "pwm_min", pref("PWM_MIN", "0"));
        pwmMaxInput = paramRow(card, "pwm_max", pref("PWM_MAX", "950"));
        safePwmInput = paramRow(card, "safe_pwm", pref("SAFE_PWM", "150"));
        filterInput = paramRow(card, "filter_alpha", pref("FILTER_ALPHA", "0.35"));
        targetMinInput = paramRow(card, "target_min", pref("TARGET_MIN", "30"));
        targetMaxInput = paramRow(card, "target_max", pref("TARGET_MAX", "70"));
        stepInput = paramRow(card, "step_cm", pref("STEP", "5"));
        tempInput = paramRow(card, "temperature", pref("TEMP", "25"));

        LinearLayout btns = row();
        Button read = button("读取当前参数", Color.WHITE, Color.rgb(31, 41, 55));
        Button apply = button("发送参数", Color.rgb(37, 99, 235), Color.WHITE);
        read.setOnClickListener(v -> sendCommand("CFG?"));
        apply.setOnClickListener(v -> sendParamCommands());
        btns.addView(read, weight(1f, 48));
        btns.addView(apply, weight(1f, 48));
        card.addView(btns);
    }

    private void showCalPage() {
        selectNav(navCal);
        content.removeAllViews();

        LinearLayout card = card();
        content.addView(card);
        card.addView(sectionTitle("测距校准表"));
        card.addView(smallText("每行填写：超声波测得值 -> 真实值。发送后单片机会按当前温度表做分段线性插值。"));

        LinearLayout tempRow = row();
        TextView tempLabel = smallText("选择温度档位 ℃");
        tempLabel.setGravity(Gravity.CENTER_VERTICAL);
        calTempSpinner = tempSpinner(pref("CAL_TEMP", "25"));
        tempRow.addView(tempLabel, weight(1.1f, 44));
        tempRow.addView(calTempSpinner, weight(1.3f, 44));
        card.addView(tempRow);

        for (int i = 0; i < 11; i++) {
            int trueCm = 20 + i * 5;
            LinearLayout r = row();
            TextView label = smallText(trueCm + " cm");
            label.setGravity(Gravity.CENTER_VERTICAL);
            calMeasuredInputs[i] = edit(pref("CAL_M_" + i, String.valueOf(trueCm)), true);
            calTrueInputs[i] = edit(pref("CAL_T_" + i, String.valueOf(trueCm)), true);
            r.addView(label, weight(0.8f, 46));
            r.addView(calMeasuredInputs[i], weight(1.2f, 46));
            r.addView(calTrueInputs[i], weight(1.2f, 46));
            card.addView(r);
        }

        LinearLayout btns = row();
        Button identity = button("填入等值表", Color.WHITE, Color.rgb(31, 41, 55));
        Button send = button("发送并应用", Color.rgb(22, 101, 52), Color.WHITE);
        identity.setOnClickListener(v -> fillIdentityCalibration());
        send.setOnClickListener(v -> sendCalibrationCommands());
        btns.addView(identity, weight(1f, 48));
        btns.addView(send, weight(1f, 48));
        card.addView(btns);
    }

    private void connectWifi() {
        if (running) {
            toast("已经连接，请先断开");
            return;
        }
        String host = value(ipInput);
        int port;
        try {
            port = Integer.parseInt(value(portInput));
        } catch (NumberFormatException ex) {
            toast("端口格式错误");
            return;
        }

        running = true;
        setLink("WiFi 连接中：" + host + ":" + port);
        readerThread = new Thread(() -> {
            try {
                Socket s = new Socket();
                s.connect(new InetSocketAddress(host, port), 5000);
                s.setKeepAlive(true);
                wifiSocket = s;
                outputStream = s.getOutputStream();
                postConnected("WiFi 已连接：" + host + ":" + port);
                readLoop(s.getInputStream());
            } catch (IOException ex) {
                postError("WiFi 错误：" + ex.getMessage());
            } finally {
                running = false;
                closeSockets();
            }
        }, "wifi-reader");
        readerThread.start();
    }

    private void connectBluetooth() {
        if (running) {
            toast("已经连接，请先断开");
            return;
        }
        if (!hasBluetoothPermission()) {
            requestBluetoothPermission();
            return;
        }
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            toast("请先打开手机蓝牙");
            return;
        }
        String target = value(btInput);
        BluetoothDevice device = findBondedDevice(adapter, target);
        if (device == null) {
            toast("未找到已配对设备：" + target);
            return;
        }

        running = true;
        setLink("蓝牙连接中：" + safeName(device));
        readerThread = new Thread(() -> {
            try {
                BluetoothSocket bs = device.createRfcommSocketToServiceRecord(SPP_UUID);
                adapter.cancelDiscovery();
                bs.connect();
                bluetoothSocket = bs;
                outputStream = bs.getOutputStream();
                postConnected("蓝牙已连接：" + safeName(device));
                readLoop(bs.getInputStream());
            } catch (IOException | SecurityException ex) {
                postError("蓝牙错误：" + ex.getMessage());
            } finally {
                running = false;
                closeSockets();
            }
        }, "bt-reader");
        readerThread.start();
    }

    private boolean hasBluetoothPermission() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestBluetoothPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT}, REQ_BT_CONNECT);
        }
    }

    private BluetoothDevice findBondedDevice(BluetoothAdapter adapter, String target) {
        try {
            Set<BluetoothDevice> devices = adapter.getBondedDevices();
            for (BluetoothDevice d : devices) {
                String name = safeName(d);
                String address = d.getAddress();
                if (target.length() == 0 ||
                        name.equalsIgnoreCase(target) ||
                        address.equalsIgnoreCase(target) ||
                        name.toLowerCase(Locale.US).contains(target.toLowerCase(Locale.US))) {
                    return d;
                }
            }
        } catch (SecurityException ignored) {
        }
        return null;
    }

    private String safeName(BluetoothDevice device) {
        try {
            String name = device.getName();
            return name == null ? device.getAddress() : name;
        } catch (SecurityException ex) {
            return "Bluetooth";
        }
    }

    private void readLoop(InputStream input) throws IOException {
        BufferedReader reader = new BufferedReader(new InputStreamReader(input));
        String line;
        while (running && (line = reader.readLine()) != null) {
            final String rxLine = line.trim();
            mainHandler.post(() -> handleLine(rxLine));
        }
    }

    private void disconnect() {
        running = false;
        closeSockets();
        setLink("未连接");
        appendLog("DISCONNECTED");
    }

    private void closeSockets() {
        try {
            if (wifiSocket != null) wifiSocket.close();
        } catch (IOException ignored) {
        }
        try {
            if (bluetoothSocket != null) bluetoothSocket.close();
        } catch (IOException ignored) {
        }
        wifiSocket = null;
        bluetoothSocket = null;
        outputStream = null;
    }

    private void sendCommand(String command) {
        ArrayList<String> cmds = new ArrayList<>();
        cmds.add(command);
        sendCommands(cmds);
    }

    private void sendCommands(List<String> commands) {
        if (commands == null || commands.isEmpty()) return;
        if (!running || outputStream == null) {
            toast("请先连接 WiFi 或蓝牙");
            return;
        }
        new Thread(() -> {
            try {
                synchronized (writeLock) {
                    for (String raw : commands) {
                        String cmd = raw == null ? "" : raw.trim();
                        if (cmd.length() == 0) continue;
                        outputStream.write((cmd + "\n").getBytes("UTF-8"));
                        outputStream.flush();
                        final String shown = cmd;
                        mainHandler.post(() -> appendLog("TX " + shown));
                        try {
                            Thread.sleep(12);
                        } catch (InterruptedException ignored) {
                        }
                    }
                }
            } catch (IOException ex) {
                mainHandler.post(() -> {
                    appendLog("SEND ERROR " + ex.getMessage());
                    toast("发送失败");
                });
            }
        }, "cmd-sender").start();
    }

    private void sendParamCommands() {
        /*
         * 先保存到手机本地，再通过串口透传发给单片机。
         * 下次连接成功时，buildSavedSyncCommands() 会读取这些保存值自动同步。
         */
        saveParamFields();
        ArrayList<String> cmds = new ArrayList<>();
        cmds.add("CFG,KP," + value(kpInput));
        cmds.add("CFG,KI," + value(kiInput));
        cmds.add("CFG,KD," + value(kdInput));
        cmds.add("CFG,BASE_PWM," + value(basePwmInput));
        cmds.add("CFG,PWM_MIN," + value(pwmMinInput));
        cmds.add("CFG,PWM_MAX," + value(pwmMaxInput));
        cmds.add("CFG,SAFE_PWM," + value(safePwmInput));
        cmds.add("CFG,FILTER_ALPHA," + value(filterInput));
        cmds.add("CFG,TARGET_MIN," + value(targetMinInput));
        cmds.add("CFG,TARGET_MAX," + value(targetMaxInput));
        cmds.add("CFG,STEP," + value(stepInput));
        cmds.add("CFG,TEMP," + value(tempInput));
        cmds.add("CFG?");
        sendCommands(cmds);
    }

    private void sendCalibrationCommands() {
        /*
         * 校准表同样保存在手机本地。
         * 单片机端收到 CAL,TEMP 后开始写临时表，收到所有 CAL,POINT 后，
         * 最后通过 CAL,APPLY 校验并应用，避免半张表进入控制算法。
         */
        saveCalibrationFields();
        ArrayList<String> cmds = new ArrayList<>();
        cmds.add("CAL,TEMP," + selectedCalTemp());
        for (int i = 0; i < 11; i++) {
            cmds.add("CAL,POINT," + i + "," + value(calMeasuredInputs[i]) + "," + value(calTrueInputs[i]));
        }
        cmds.add("CAL,APPLY");
        sendCommands(cmds);
    }

    private void fillIdentityCalibration() {
        for (int i = 0; i < 11; i++) {
            int v = 20 + i * 5;
            calMeasuredInputs[i].setText(String.valueOf(v));
            calTrueInputs[i].setText(String.valueOf(v));
        }
    }

    private String pref(String key, String defValue) {
        return prefs == null ? defValue : prefs.getString(key, defValue);
    }

    private void saveParamFields() {
        SharedPreferences.Editor e = prefs.edit();
        e.putString("KP", value(kpInput));
        e.putString("KI", value(kiInput));
        e.putString("KD", value(kdInput));
        e.putString("BASE_PWM", value(basePwmInput));
        e.putString("PWM_MIN", value(pwmMinInput));
        e.putString("PWM_MAX", value(pwmMaxInput));
        e.putString("SAFE_PWM", value(safePwmInput));
        e.putString("FILTER_ALPHA", value(filterInput));
        e.putString("TARGET_MIN", value(targetMinInput));
        e.putString("TARGET_MAX", value(targetMaxInput));
        e.putString("STEP", value(stepInput));
        e.putString("TEMP", value(tempInput));
        e.apply();
    }

    private void saveCalibrationFields() {
        SharedPreferences.Editor e = prefs.edit();
        e.putString("CAL_TEMP", selectedCalTemp());
        for (int i = 0; i < 11; i++) {
            e.putString("CAL_M_" + i, value(calMeasuredInputs[i]));
            e.putString("CAL_T_" + i, value(calTrueInputs[i]));
        }
        e.apply();
    }

    private ArrayList<String> buildSavedSyncCommands() {
        /*
         * 连接成功后的自动同步命令队列。
         * 这里不依赖单片机 Flash，调试数据由 App 保存；单片机每次上电后，
         * 只要 App 重新连接，就能恢复上一次调好的 PID 和校准表。
         */
        ArrayList<String> cmds = new ArrayList<>();
        cmds.add("CFG,KP," + pref("KP", "22.0"));
        cmds.add("CFG,KI," + pref("KI", "1.5"));
        cmds.add("CFG,KD," + pref("KD", "6.0"));
        cmds.add("CFG,BASE_PWM," + pref("BASE_PWM", "350"));
        cmds.add("CFG,PWM_MIN," + pref("PWM_MIN", "0"));
        cmds.add("CFG,PWM_MAX," + pref("PWM_MAX", "950"));
        cmds.add("CFG,SAFE_PWM," + pref("SAFE_PWM", "150"));
        cmds.add("CFG,FILTER_ALPHA," + pref("FILTER_ALPHA", "0.35"));
        cmds.add("CFG,TARGET_MIN," + pref("TARGET_MIN", "30"));
        cmds.add("CFG,TARGET_MAX," + pref("TARGET_MAX", "70"));
        cmds.add("CFG,STEP," + pref("STEP", "5"));
        cmds.add("CFG,TEMP," + pref("TEMP", "25"));
        cmds.add("CAL,TEMP," + pref("CAL_TEMP", "25"));
        for (int i = 0; i < 11; i++) {
            int trueCm = 20 + i * 5;
            cmds.add("CAL,POINT," + i + "," +
                    pref("CAL_M_" + i, String.valueOf(trueCm)) + "," +
                    pref("CAL_T_" + i, String.valueOf(trueCm)));
        }
        cmds.add("CAL,APPLY");
        cmds.add("CFG?");
        cmds.add("GET");
        return cmds;
    }

    private void syncSavedSettingsAfterConnect() {
        /*
         * 延迟 250ms 再同步，给 Socket/蓝牙输出流一点稳定时间。
         * 同步完成后 App 会发送 CFG? 和 GET，便于日志里看到单片机确认状态。
         */
        mainHandler.postDelayed(() -> {
            appendLog("AUTO SYNC saved params");
            sendCommands(buildSavedSyncCommands());
        }, 250);
    }

    private void handleLine(String line) {
        if (line.length() == 0) return;
        appendLog("RX " + line);

        String[] parts = line.split(",");
        if (parts.length >= 6 && "T".equals(parts[0])) {
            try {
                boolean active = "1".equals(parts[2]);
                float target = Float.parseFloat(parts[3]);
                float actual = Float.parseFloat(parts[4]);
                int pwm = Integer.parseInt(parts[5]);
                int rpm = parts.length >= 7 ? Integer.parseInt(parts[6]) : 0;
                float temp = parts.length >= 8 ? Float.parseFloat(parts[7]) : Float.NaN;

                setTextIfPresent(stateText, active ? "ACTIVE" : "STANDBY");
                setTextIfPresent(targetText, one(target));
                setTextIfPresent(actualText, actual >= 0.0f ? one(actual) : "ERR");
                setTextIfPresent(pwmText, String.valueOf(pwm));
                setTextIfPresent(rpmText, String.valueOf(rpm));
                setTextIfPresent(tempText, Float.isNaN(temp) ? "--" : one(temp));
                if (chartView != null) {
                    chartView.addPoint(target, actual >= 0.0f ? actual : Float.NaN, pwm, rpm);
                }
            } catch (NumberFormatException ex) {
                appendLog("PARSE ERROR " + ex.getMessage());
            }
        } else if (parts.length >= 13 && "C".equals(parts[0])) {
            updateParamFields(parts);
        }
    }

    private void updateParamFields(String[] p) {
        setEditIfPresent(kpInput, p[1]);
        setEditIfPresent(kiInput, p[2]);
        setEditIfPresent(kdInput, p[3]);
        setEditIfPresent(basePwmInput, p[4]);
        setEditIfPresent(pwmMinInput, p[5]);
        setEditIfPresent(pwmMaxInput, p[6]);
        setEditIfPresent(safePwmInput, p[7]);
        setEditIfPresent(filterInput, p[8]);
        setEditIfPresent(targetMinInput, p[9]);
        setEditIfPresent(targetMaxInput, p[10]);
        setEditIfPresent(stepInput, p[11]);
        setEditIfPresent(tempInput, p[12]);
    }

    private void postConnected(String text) {
        mainHandler.post(() -> {
            setLink(text);
            appendLog("CONNECTED " + text);
            syncSavedSettingsAfterConnect();
        });
    }

    private void postError(String text) {
        mainHandler.post(() -> {
            setLink(text);
            appendLog("ERROR " + text);
        });
    }

    private void setLink(String text) {
        mainHandler.post(() -> {
            if (linkText != null) linkText.setText(text);
        });
    }

    private void appendLog(String text) {
        if (logText == null) return;
        String old = logText.getText().toString();
        String next = text + "\n" + old;
        if (next.length() > 4500) {
            next = next.substring(0, 4500);
        }
        logText.setText(next);
    }

    private void selectNav(Button active) {
        Button[] buttons = {navControl, navChart, navParam, navCal};
        for (Button b : buttons) {
            if (b == null) continue;
            boolean isActive = (b == active);
            // Google Play bottom nav style: light blue pill for active
            int targetBg = isActive ? Color.rgb(232, 240, 254) : Color.TRANSPARENT;
            int targetFg = isActive ? Color.rgb(26, 115, 232) : Color.rgb(95, 99, 104);
            
            b.setTextColor(targetFg);
            GradientDrawable shape = new GradientDrawable();
            shape.setColor(targetBg);
            shape.setCornerRadius(dp(24));
            
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                b.setBackground(new android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(Color.argb(30, 0,0,0)), shape, null));
            } else {
                b.setBackground(shape);
            }
            b.setTypeface(android.graphics.Typeface.create(
                isActive ? "sans-serif-medium" : "sans-serif", android.graphics.Typeface.NORMAL));
        }
    }

    private LinearLayout card() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(dp(16), dp(16), dp(16), dp(16));
        
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.WHITE);
        bg.setCornerRadius(dp(16)); // 大圆角
        layout.setBackground(bg);
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            layout.setElevation(dp(3)); // 卡片阴影
        }

        LinearLayout.LayoutParams lp = matchWrap();
        lp.setMargins(dp(8), dp(8), dp(8), dp(8));
        layout.setLayoutParams(lp);
        return layout;
    }

    private TextView sectionTitle(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        setTextDp(view, 17);
        view.setIncludeFontPadding(false);
        view.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
        view.setTextColor(Color.rgb(32, 33, 36));
        view.setPadding(0, 0, 0, dp(10));
        return view;
    }

    private TextView smallText(String text) {
        TextView view = new TextView(this);
        view.setText(text);
        setTextDp(view, 13);
        view.setIncludeFontPadding(false);
        view.setTextColor(Color.rgb(95, 99, 104));
        view.setPadding(0, dp(4), 0, dp(4));
        return view;
    }

    private TextView metric(LinearLayout parent, String label, String value) {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(14), dp(12), dp(14), dp(12));
        
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.WHITE);
        bg.setCornerRadius(dp(14));
        box.setBackground(bg);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            box.setElevation(dp(3));
        }

        TextView l = smallText(label);
        TextView v = new TextView(this);
        v.setText(value);
        setTextDp(v, 24);
        v.setIncludeFontPadding(false);
        v.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
        v.setTextColor(Color.rgb(26, 115, 232)); // 主题蓝
        box.addView(l);
        box.addView(v);

        LinearLayout.LayoutParams lp = weight(1f, -1);
        lp.setMargins(dp(6), dp(8), dp(6), dp(8));
        parent.addView(box, lp);
        return v;
    }

    private EditText paramRow(LinearLayout parent, String label, String value) {
        LinearLayout row = row();
        row.setPadding(0, dp(2), 0, dp(2));
        TextView l = smallText(label);
        l.setGravity(Gravity.CENTER_VERTICAL);
        EditText e = edit(value, true);
        row.addView(l, weight(1.1f, 46));
        row.addView(e, weight(1.3f, 46));
        parent.addView(row);
        return e;
    }

    private LinearLayout row() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        return layout;
    }

    private EditText edit(String text, boolean decimal) {
        EditText e = new EditText(this);
        e.setText(text);
        e.setSingleLine(true);
        setTextDp(e, 15);
        e.setIncludeFontPadding(false);
        e.setPadding(dp(12), 0, dp(12), 0);
        
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.rgb(241, 243, 244)); // Google Play Input background
        bg.setCornerRadius(dp(8));
        e.setBackground(bg);
        e.setTextColor(Color.rgb(32, 33, 36));
        
        if (decimal) {
            e.setInputType(InputType.TYPE_CLASS_NUMBER |
                    InputType.TYPE_NUMBER_FLAG_DECIMAL |
                    InputType.TYPE_NUMBER_FLAG_SIGNED);
        }
        return e;
    }

    private Spinner tempSpinner(String selected) {
        Spinner spinner = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(
                this, android.R.layout.simple_spinner_item, CAL_TEMP_OPTIONS) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                TextView view = (TextView) super.getView(position, convertView, parent);
                styleSpinnerText(view, position);
                return view;
            }

            @Override
            public View getDropDownView(int position, View convertView, ViewGroup parent) {
                TextView view = (TextView) super.getDropDownView(position, convertView, parent);
                styleSpinnerText(view, position);
                return view;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        spinner.setPadding(dp(10), 0, dp(10), 0);
        
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.rgb(241, 243, 244));
        bg.setCornerRadius(dp(8));
        spinner.setBackground(bg);
        
        for (int i = 0; i < CAL_TEMP_OPTIONS.length; i++) {
            if (CAL_TEMP_OPTIONS[i].equals(selected)) {
                spinner.setSelection(i);
                break;
            }
        }
        return spinner;
    }

    private Button button(String text, int bg, int fg) {
        Button b = new Button(this);
        b.setText(text);
        setTextDp(b, 15);
        b.setIncludeFontPadding(false);
        b.setTextColor(fg);
        b.setAllCaps(false);
        b.setTypeface(android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL));
        
        int finalBg = (bg == Color.WHITE) ? Color.rgb(241, 243, 244) : bg;
        
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(finalBg);
        shape.setCornerRadius(dp(24)); // 胶囊形状按钮
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            int rippleColor = (fg == Color.WHITE) ? Color.argb(60, 255, 255, 255) : Color.argb(40, 0, 0, 0);
            b.setBackground(new android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(rippleColor), shape, null));
        } else {
            b.setBackground(shape);
        }
        
        // 按压缩放动效
        b.setOnTouchListener((v, event) -> {
            switch (event.getAction()) {
                case android.view.MotionEvent.ACTION_DOWN:
                    v.animate().scaleX(0.96f).scaleY(0.96f).setDuration(100).start();
                    break;
                case android.view.MotionEvent.ACTION_UP:
                case android.view.MotionEvent.ACTION_CANCEL:
                    v.animate().scaleX(1.0f).scaleY(1.0f).setDuration(150).start();
                    break;
            }
            return false;
        });
        return b;
    }

    private Button navButton(String text, View.OnClickListener l) {
        // Nav 按钮不要背景和缩放，专门处理
        Button b = new Button(this);
        b.setText(text);
        setTextDp(b, 14);
        b.setIncludeFontPadding(false);
        b.setTextColor(Color.rgb(95, 99, 104));
        b.setAllCaps(false);
        b.setBackgroundColor(Color.TRANSPARENT);
        b.setOnClickListener(l);
        return b;
    }

    private GradientDrawable round(int color, int radius, int strokeColor) {
        // 保留空方法防报错
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radius);
        return d;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams matchHeight(int h) {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(h));
    }

    private LinearLayout.LayoutParams matchButton() {
        LinearLayout.LayoutParams lp = matchHeight(48);
        lp.setMargins(0, dp(8), 0, 0);
        return lp;
    }

    private LinearLayout.LayoutParams weight(float weight, int h) {
        int height = h < 0 ? LinearLayout.LayoutParams.WRAP_CONTENT : dp(h);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, height, weight);
        lp.setMargins(0, dp(8), dp(8), 0);
        return lp;
    }

    private String value(EditText e) {
        return e == null ? "" : e.getText().toString().trim();
    }

    private String selectedCalTemp() {
        if (calTempSpinner == null || calTempSpinner.getSelectedItem() == null) {
            return pref("CAL_TEMP", "25");
        }
        return String.valueOf(calTempSpinner.getSelectedItem()).trim();
    }

    private void setTextDp(TextView view, float sizeDp) {
        view.setTextSize(TypedValue.COMPLEX_UNIT_DIP, sizeDp);
    }

    private void styleSpinnerText(TextView view, int position) {
        view.setText(CAL_TEMP_OPTIONS[position] + " ℃");
        setTextDp(view, 14);
        view.setIncludeFontPadding(false);
        view.setTextColor(Color.rgb(15, 23, 42));
        view.setGravity(Gravity.CENTER_VERTICAL);
    }

    private String one(float value) {
        return String.format(Locale.US, "%.1f", value);
    }

    private void setTextIfPresent(TextView view, String text) {
        if (view != null) view.setText(text);
    }

    private void setEditIfPresent(EditText view, String text) {
        if (view != null) view.setText(text);
    }

    private void toast(String text) {
        Toast.makeText(this, text, Toast.LENGTH_SHORT).show();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    public static class ChartView extends View {
        private static final int MAX_POINTS = 180;
        private final List<Float> targets = new ArrayList<>();
        private final List<Float> actuals = new ArrayList<>();
        private final List<Float> errors = new ArrayList<>();
        private final List<Integer> pwms = new ArrayList<>();
        private final List<Integer> rpms = new ArrayList<>();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        public ChartView(android.content.Context context) {
            super(context);
            setBackgroundColor(Color.WHITE);
        }

        public void addPoint(float target, float actual, int pwm, int rpm) {
            targets.add(target);
            actuals.add(actual);
            errors.add(Float.isNaN(actual) ? Float.NaN : target - actual);
            pwms.add(pwm);
            rpms.add(rpm);
            while (targets.size() > MAX_POINTS) {
                targets.remove(0);
                actuals.remove(0);
                errors.remove(0);
                pwms.remove(0);
                rpms.remove(0);
            }
            invalidate();
        }

        public void clear() {
            targets.clear();
            actuals.clear();
            errors.clear();
            pwms.clear();
            rpms.clear();
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            int w = getWidth();
            int h = getHeight();
            int left = 44;
            int right = w - 10;
            int top1 = 50;
            int bottom1 = h / 2 - 18;
            int top2 = h / 2 + 54;
            int bottom2 = h - 30;

            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.WHITE);
            canvas.drawRoundRect(new RectF(0, 0, w, h), 16, 16, paint);

            paint.setStyle(Paint.Style.FILL);
            paint.setTextSize(17f);
            paint.setColor(Color.rgb(15, 23, 42));
            canvas.drawText("PID tracking", left, 18, paint);
            canvas.drawText("Fan speed RPM", left, h / 2 + 22, paint);

            paint.setTextSize(13f);
            int x1 = left;
            x1 = legend(canvas, x1, 38, "目标", Color.rgb(220, 38, 38));
            x1 = legend(canvas, x1, 38, "实测", Color.rgb(22, 163, 74));
            legend(canvas, x1, 38, "误差", Color.rgb(234, 88, 12));

            int x2 = left;
            x2 = legend(canvas, x2, h / 2 + 42, "RPM", Color.rgb(147, 51, 234));
            legend(canvas, x2, h / 2 + 42, "PWM", Color.rgb(37, 99, 235));

            drawCmGrid(canvas, left, right, top1, bottom1);
            drawRpmGrid(canvas, left, right, top2, bottom2);

            drawFloatSeries(canvas, targets, left, right, top1, bottom1, 20f, 80f, Color.rgb(220, 38, 38));
            drawFloatSeries(canvas, actuals, left, right, top1, bottom1, 20f, 80f, Color.rgb(22, 163, 74));
            drawFloatSeries(canvas, errors, left, right, top1, bottom1, -20f, 20f, Color.rgb(234, 88, 12));

            int maxRpm = 1000;
            for (Integer rpm : rpms) {
                if (rpm != null && rpm > maxRpm) maxRpm = rpm;
            }
            drawIntSeries(canvas, rpms, left, right, top2, bottom2, 0, maxRpm, Color.rgb(147, 51, 234));
            drawIntSeries(canvas, pwms, left, right, top2, bottom2, 0, 1000, Color.rgb(37, 99, 235));
        }

        private void drawCmGrid(Canvas canvas, int left, int right, int top, int bottom) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(1f);
            paint.setColor(Color.rgb(226, 232, 240));
            canvas.drawRect(new RectF(left, top, right, bottom), paint);

            paint.setTextSize(13f);
            paint.setStyle(Paint.Style.FILL);
            for (int y = 20; y <= 80; y += 20) {
                float py = map(y, 20, 80, bottom, top);
                paint.setColor(Color.rgb(100, 116, 139));
                canvas.drawText(String.valueOf(y), 8, py + 5, paint);
                paint.setColor(Color.rgb(241, 245, 249));
                canvas.drawLine(left, py, right, py, paint);
            }
        }

        private void drawRpmGrid(Canvas canvas, int left, int right, int top, int bottom) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(1f);
            paint.setColor(Color.rgb(226, 232, 240));
            canvas.drawRect(new RectF(left, top, right, bottom), paint);

            paint.setTextSize(13f);
            paint.setStyle(Paint.Style.FILL);
            for (int p = 0; p <= 1000; p += 500) {
                float py = map(p, 0, 1000, bottom, top);
                paint.setColor(Color.rgb(100, 116, 139));
                canvas.drawText(String.valueOf(p), 8, py + 5, paint);
                paint.setColor(Color.rgb(241, 245, 249));
                canvas.drawLine(left, py, right, py, paint);
            }
        }

        private int legend(Canvas canvas, int x, int y, String text, int color) {
            paint.setColor(color);
            canvas.drawCircle(x + 6, y - 5, 4, paint);
            paint.setColor(Color.rgb(51, 65, 85));
            canvas.drawText(text, x + 16, y, paint);
            return x + (int) paint.measureText(text) + 34;
        }

        private void drawFloatSeries(Canvas canvas, List<Float> data, int left, int right,
                                     int top, int bottom, float min, float max, int color) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2.8f);
            paint.setColor(color);
            boolean started = false;
            float px = 0, py = 0;
            for (int i = 0; i < data.size(); i++) {
                float value = data.get(i);
                if (Float.isNaN(value)) {
                    started = false;
                    continue;
                }
                float x = left + (right - left) * (i / (float) Math.max(1, MAX_POINTS - 1));
                float y = map(value, min, max, bottom, top);
                if (started) canvas.drawLine(px, py, x, y, paint);
                px = x;
                py = y;
                started = true;
            }
        }

        private void drawIntSeries(Canvas canvas, List<Integer> data, int left, int right,
                                   int top, int bottom, int min, int max, int color) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2.5f);
            paint.setColor(color);
            boolean started = false;
            float px = 0, py = 0;
            for (int i = 0; i < data.size(); i++) {
                int value = data.get(i);
                float x = left + (right - left) * (i / (float) Math.max(1, MAX_POINTS - 1));
                float y = map(value, min, max, bottom, top);
                if (started) canvas.drawLine(px, py, x, y, paint);
                px = x;
                py = y;
                started = true;
            }
        }

        private float map(float v, float min, float max, int bottom, int top) {
            if (max <= min) return bottom;
            float clamped = Math.max(min, Math.min(max, v));
            return bottom - (clamped - min) / (max - min) * (bottom - top);
        }
    }
}
