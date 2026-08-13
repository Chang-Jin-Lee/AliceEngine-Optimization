#include "Runtime/Engine/EngineImpl.h"

#include <shellapi.h>

namespace Alice
{
	namespace
	{
		constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";
	}

	bool Engine::Impl::CreateMainWindow(Engine& owner, int nCmdShow)
	{
		// ============================================= 아이콘 로드 =============================================
		// CMake에서 exe에 임베딩된 아이콘을 사용합니다.
		constexpr wchar_t kAppIconName[] = L"IDI_ICON1";
		HICON hIconBig = LoadIconW(m_hInstance, kAppIconName);
		HICON hIconSmall = LoadIconW(m_hInstance, kAppIconName);

		// ============================================= 윈도우 클래스 등록 =============================================
		// C++ 구조체 제로 초기화({})를 활용하여 불필요한 0 대입 생략
		WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = &Engine::WindowProc;
		wc.hInstance = m_hInstance;
		wc.hIcon = hIconBig ? hIconBig : LoadIcon(nullptr, IDI_APPLICATION);     // Fallback 처리
		wc.hIconSm = hIconSmall ? hIconSmall : LoadIcon(nullptr, IDI_APPLICATION);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszClassName = kWindowClassName;

		if (!RegisterClassExW(&wc)) return false;

		// ============================================= 실제 윈도우 크기 계산 =============================================
		// Client Size -> Window Size
		const DWORD windowStyle = m_commandLine.benchRequested
			? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU)
			: WS_OVERLAPPEDWINDOW;
		RECT rc = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
		AdjustWindowRect(&rc, windowStyle, FALSE);

		// ============================================= 윈도우 생성 =============================================
		// Engine 포인터 전달
		m_hWnd = CreateWindowExW(
			0, kWindowClassName, L"AliceRenderer", windowStyle,
			CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, // 계산된 너비/높이 바로 사용
			nullptr, nullptr, m_hInstance, &owner
		);

		if (!m_hWnd) return false;

		if (m_editorMode)
			DragAcceptFiles(m_hWnd, TRUE);

		ShowWindow(m_hWnd, nCmdShow);
		UpdateWindow(m_hWnd);

		return true;
	}

	void Engine::Impl::OnResize(std::uint32_t width, std::uint32_t height)
	{
		m_width = width;
		m_height = height;

		// 디바이스 리사이즈 및 카메라 종횡비 갱신
		if (m_renderDevice)
		{
			m_renderDevice->Resize(width, height);

			// 높이가 0이어도 안전하게 1로 처리하여 계산
			const float aspect = static_cast<float>(width) / (std::max)(height, 1u);
			m_camera.SetPerspective(DirectX::XM_PIDIV4, aspect, 0.1f, 100.0f);
		}

		// 렌더러 리사이즈 (텍스처 재생성 등)
		if (m_forwardRenderSystem)
		{
			m_forwardRenderSystem->Resize(width, height);
		}
		if (m_deferredRenderSystem)
		{
			m_deferredRenderSystem->Resize(width, height);
		}
		if (m_computeEffectSystem)
		{
			m_computeEffectSystem->Resize(width, height);
		}

	}

	void Engine::Impl::ToggleBorderlessFullscreen()
	{
		if (!m_hWnd || m_commandLine.benchRequested)
			return;

		if (!m_borderlessFullscreen)
		{
			m_windowedStyle = GetWindowLongW(m_hWnd, GWL_STYLE);
			m_windowedExStyle = GetWindowLongW(m_hWnd, GWL_EXSTYLE);
			GetWindowRect(m_hWnd, &m_windowedRect);

			MONITORINFO mi{ sizeof(mi) };
			if (GetMonitorInfoW(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi))
			{
				SetWindowLongW(m_hWnd, GWL_STYLE, (m_windowedStyle & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
				SetWindowLongW(m_hWnd, GWL_EXSTYLE, m_windowedExStyle);
				SetWindowPos(
					m_hWnd,
					HWND_TOP,
					mi.rcMonitor.left,
					mi.rcMonitor.top,
					mi.rcMonitor.right - mi.rcMonitor.left,
					mi.rcMonitor.bottom - mi.rcMonitor.top,
					SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
				m_borderlessFullscreen = true;
			}
		}
		else
		{
			SetWindowLongW(m_hWnd, GWL_STYLE, m_windowedStyle);
			SetWindowLongW(m_hWnd, GWL_EXSTYLE, m_windowedExStyle);
			SetWindowPos(
				m_hWnd,
				HWND_NOTOPMOST,
				m_windowedRect.left,
				m_windowedRect.top,
				m_windowedRect.right - m_windowedRect.left,
				m_windowedRect.bottom - m_windowedRect.top,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			m_borderlessFullscreen = false;
		}
	}

	LRESULT Engine::Impl::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto ReleaseMouseLockOnDeactivate = [&]()
			{
				m_inputSystem.NotifyAppActivated(false);
				m_inputSystem.SetCursorLocked(false);
				m_inputSystem.SetCursorVisible(true);

				for (auto&& [id, follow] : m_world.GetComponents<CameraFollowComponent>())
				{
					(void)id;
					follow.mouseLocked = false;
				}
			};

		switch (message)
		{
		case WM_SIZE:
			// Bench backbuffer dimensions are part of the measurement contract.
			if (m_commandLine.benchRequested)
				return 0;
			if (wParam == SIZE_MINIMIZED)
			{
				ReleaseMouseLockOnDeactivate();
			}
			// 리사이즈
			// lParam의 하위/상위 워드에서 해상도 추출 후 즉시 반영
			OnResize(static_cast<std::uint32_t>(LOWORD(lParam)), static_cast<std::uint32_t>(HIWORD(lParam)));
			return 0;

		case WM_ACTIVATEAPP:
			if (wParam == FALSE)
			{
				ReleaseMouseLockOnDeactivate();
			}
			else
			{
				m_inputSystem.NotifyAppActivated(true);
			}
			return 0;

		case WM_KILLFOCUS:
			ReleaseMouseLockOnDeactivate();
			return 0;

		case WM_SETFOCUS:
			m_inputSystem.NotifyAppActivated(true);
			return 0;

		case WM_SYSKEYDOWN:
			if (wParam == VK_RETURN && (HIWORD(lParam) & KF_ALTDOWN))
			{
				if (m_commandLine.benchRequested)
					return 0;
				ToggleBorderlessFullscreen();
				if (m_inputSystem.IsCursorLocked())
				{
					m_inputSystem.SetCursorLocked(true);
				}
				return 0;
			}
			break;

		case WM_SYSCOMMAND:
			if (m_commandLine.benchRequested)
			{
				const WPARAM command = wParam & 0xfff0;
				if (command == SC_SIZE || command == SC_MINIMIZE || command == SC_MAXIMIZE)
					return 0;
			}
			break;

		case WM_DESTROY:
			// 종료
			// 메인 루프 플래그 해제 및 종료 메시지 전송
			m_isRunning = false;
			PostQuitMessage(0);
			return 0;

		case WM_DROPFILES:
		{
			HDROP drop = reinterpret_cast<HDROP>(wParam);
			const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
			std::vector<std::filesystem::path> files;
			files.reserve(count);
			for (UINT i = 0; i < count; ++i)
			{
				wchar_t buf[MAX_PATH] = {};
				if (DragQueryFileW(drop, i, buf, MAX_PATH) > 0)
					files.emplace_back(buf);
			}
			DragFinish(drop);

			if (m_dropFilesHandler && !files.empty())
				m_dropFilesHandler(files);
			return 0;
		}

		case WM_INPUT:
			// Raw Input 처리
			m_inputSystem.ProcessRawInput(reinterpret_cast<HRAWINPUT>(lParam));
			return 0;
		}

		return DefWindowProcW(hWnd, message, wParam, lParam);
	}

}
