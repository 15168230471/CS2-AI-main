#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <qobject.h>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>

#include "CS2/CS2AI.h"
#include "CS2/NavmeshPoints.h"
#include "CS2/ConfigReader.h"

// ����ģʽ
enum class ModeRunning
{
    NONE = 0,
    AI,
    POINT_CREATOR
};

class CS2Runner : public QObject
{
    Q_OBJECT

public:
    CS2Runner();

    // ����ѭ����ÿ֡����
    void update();

    // ��ǰ����������� CS2 �ͻ���
    void focusAndClipCS2Window();

    // �������ýӿ�
    void set_mode(ModeRunning mode);
    void set_add_point_key(int key_code);
    bool save_navmesh_points();
    void add_point();
    void load_config();
    void load_offsets();
    void load_navmesh();
    void attach_to_process(); // ���Ը��� CS2 ���̣�����������

    void set_activated_behavior(const ActivatedFeatures& behavior);
    std::pair<bool, Vec3D<float>> get_current_position();

    // ���ı���ͼ/�Ŷ���ڣ��ڲ���װ����ͼ�����߼���
    void check_matchmaking_status();

signals:
    void finished();

public slots:
    void run();

private:
    // ================== ʱ��/���ڿ��ƣ��������Ｏ�ж��塢ע�ͣ� ==================

    // ���� CS2 ���̵��������ڣ�ÿ 5 �볢��һ�Σ������δ���ӣ�
    const std::chrono::seconds kAttachRetryInterval{ 5 };
    std::chrono::steady_clock::time_point m_lastAttachAttempt{ std::chrono::steady_clock::now() - kAttachRetryInterval };
    std::atomic<bool> m_attached{ false }; // ����Ƿ��ѳɹ����ӵ� CS2 ����

    // ��ͨ״̬��飨�������/ѡ�ˣ����ڣ�Ŀǰ���� map/team �����ࣩ
    const std::chrono::seconds m_statusCheckInterval{ 10 };
    std::chrono::steady_clock::time_point m_lastStatusCheck{ std::chrono::steady_clock::now() };

    // ����ͼ���͵�������߼��Ľ��ࣨ���� 3 ���Ӽ��һ�� BackgroundMap��
    const std::chrono::minutes kBackgroundCheckInterval{ 3 };
    std::chrono::steady_clock::time_point m_lastBackgroundCheck{ std::chrono::steady_clock::now() - kBackgroundCheckInterval };
    bool m_sequenceAttempted = false; // ��¼�Ƿ��Ѿ�ִ�й�������У���һ�� vs �ڶ�����Ϊ��ͬ��

    // ��λ/�ȼ�������ڣ�����ÿ֡ˢ��
    const std::chrono::minutes kRankLogInterval{ 3 };
    std::chrono::steady_clock::time_point m_last_rank_log{ std::chrono::steady_clock::now() };
    int m_last_recorded_rank = -1; // ��һ����Ч�� rank / profile level�������������

    // ================== ����ģ�� ==================

    // �������/����״̬
    void check_weapon_Status();

    // ����ͼ/����״̬�������Զ�ѡ�ӵȣ�
    void check_map_team_Status();

    // �ж���ǰ�Ƿ��Ǳ���ͼ����װ�ײ� game info handler��
    bool is_background_map();

    // ��װ�Ķ�λ/�ȼ���ӡ & ��������߼�
    void log_rank_if_due();

    // ��������ڼ���Լ����̿��ƹ���
    void perform_click_sequence(HWND hwnd);
    void click_in_window(HWND hwnd, POINT clientPt);
    void terminate_process_by_name(const std::wstring& name);

    // ================== �ڲ�״̬ ==================

    std::mutex m_mutex;
    ModeRunning m_mode = ModeRunning::AI;
    bool m_is_running = true;

    std::unique_ptr<CS2Ai> m_cs2_ai_handler = nullptr;
    std::unique_ptr<NavmeshPoints> m_cs2_navmesh_points_handler = nullptr;

    // �ƻ��õ����� id ���� buy weapon once �߼�
    int plan_weapon_id = 13776; // Ĭ������

    // ����չ�������������Ϊ����
};
