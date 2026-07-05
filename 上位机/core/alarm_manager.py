#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
报警管理器
- 根据规则判断是否需要报警
- 报警入库
- 触发声音/弹窗（通过信号通知 UI）
"""

from PyQt6.QtCore import QObject, pyqtSignal

from config.settings import settings
from database.db_manager import db


class AlarmManager(QObject):
    """报警管理器"""

    alarm_triggered = pyqtSignal(str, str, str)  # device_id, alarm_type, message

    def __init__(self):
        super().__init__()

    def check_and_trigger(self, data: dict, result_id: int):
        """
        检查一条检测结果是否触发报警，并入库
        """
        device_id = data.get("device_id", "unknown")
        defect = data.get("defect", "normal")
        position_ok = data.get("position_ok", True)

        alarm_defects = set(settings.get("alarm_defect_types", ["damage", "stain", "wrinkle"]))
        offset_enabled = bool(settings.get("alarm_offset_enabled", True))

        triggered = False

        # 1. 缺陷报警
        if defect in alarm_defects:
            level = "critical" if defect == "damage" else "warning"
            self._add_alarm(
                device_id, result_id, "defect", level,
                f"[{device_id}] 发现缺陷: {defect}"
            )
            triggered = True

        # 2. 偏移报警
        if not position_ok and offset_enabled:
            offset = data.get("offset_ratio", [0.0, 0.0])
            self._add_alarm(
                device_id, result_id, "offset", "warning",
                f"[{device_id}] 标签偏移超标: offset={offset}"
            )
            triggered = True

        # 如果触发了报警，同时标记结果记录
        if triggered:
            self._mark_result_alarm(result_id)
            self.alarm_triggered.emit(device_id, "multi", f"检测到异常，请查看报警面板")

    def _add_alarm(self, device_id: str, result_id: int, alarm_type: str, alarm_level: str, message: str):
        db.insert_alarm(device_id, result_id, alarm_type, alarm_level, message)

    def _mark_result_alarm(self, result_id: int):
        conn = db._get_conn()
        conn.execute("UPDATE detection_results SET alarm_triggered = 1 WHERE id = ?", (result_id,))
        conn.commit()


alarm_manager = AlarmManager()
