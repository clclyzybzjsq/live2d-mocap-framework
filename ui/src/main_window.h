#pragma once

#include <QMainWindow>

class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;

namespace lb::ui {

class ModelsPage;
class MocapPage;
class OutputPage;
class SettingsPage;

// 主窗口。布局按方案 7.3：顶栏（唯一主按钮）+ 左导航 + 内容区 + 状态栏。
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

public slots:
    void set_running(bool running);

private:
    QWidget* build_top_bar();
    QWidget* build_status_bar();
    void build_tray();
    void refresh_status();

    // 顶栏
    QLabel* status_dot_ = nullptr;
    QLabel* status_text_ = nullptr;
    QLabel* model_name_ = nullptr;
    QPushButton* start_button_ = nullptr;

    // 导航与页面
    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;

    // 状态栏
    QLabel* status_camera_ = nullptr;
    QLabel* status_infer_ = nullptr;
    QLabel* status_render_ = nullptr;
    QLabel* status_latency_ = nullptr;
    QLabel* status_model_ = nullptr;

    // 托盘
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* tray_menu_ = nullptr;

    QTimer* ui_timer_ = nullptr;
    bool running_ = false;
};

} // namespace lb::ui