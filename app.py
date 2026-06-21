import logging
from flask import Flask, request, jsonify, render_template, make_response

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

app = Flask(__name__)

# Global state initialized with starting metrics
data_state = {
    "ppm": 0,
    "fan": "OFF",
    "buzzer": "OFF",
    "signal_dbm": -100,
    "voltage_v": 0.0,
    "uptime": "00:00:00",
    "device_status": "Idle"
}

@app.route("/")
def index():
    """Render the dashboard UI."""
    return render_template("index.html")

@app.route("/update", methods=["POST"])
def update():
    """
    Update the server data state from ESP32 JSON payload.
    Expected format: {"ppm": <int>, "fan": <int>, "buzzer": <int>}
    """
    global data_state
    try:
        logging.info(
            "Received POST /update from %s with content-type %s",
            request.remote_addr,
            request.content_type,
        )
        logging.info("Raw payload: %s", request.get_data(as_text=True))

        content = request.get_json(silent=True)
        if not content:
            logging.warning("Received update request with empty or invalid JSON payload.")
            return jsonify({"status": "error", "message": "Invalid JSON"}), 400

        # Retrieve and validate data
        ppm_val = content.get("ppm")
        fan_val = content.get("fan")
        buzzer_val = content.get("buzzer")

        if ppm_val is None or fan_val is None or buzzer_val is None:
            logging.warning(f"Missing required fields in payload: {content}")
            return jsonify({"status": "error", "message": "Missing required fields"}), 400

        def safe_int(value, default=0):
            try:
                return int(value)
            except (ValueError, TypeError):
                return default

        def safe_float(value, default=0.0):
            try:
                return float(value)
            except (ValueError, TypeError):
                return default

        def on_off(value):
            return "ON" if str(value).strip().lower() in {"1", "true", "on", "yes"} else "OFF"

        ppm = safe_int(ppm_val)
        fan = on_off(fan_val)
        buzzer = on_off(buzzer_val)

        voltage_v = safe_float(content.get("voltage_v"), data_state["voltage_v"])
        signal_dbm = safe_int(content.get("signal_dbm"), data_state["signal_dbm"])
        uptime = str(content.get("uptime", data_state["uptime"]))
        device_status = str(content.get("device_status", data_state["device_status"]))

        # Update global state
        data_state["ppm"] = ppm
        data_state["fan"] = fan
        data_state["buzzer"] = buzzer
        data_state["voltage_v"] = voltage_v
        data_state["signal_dbm"] = signal_dbm
        data_state["uptime"] = uptime
        data_state["device_status"] = device_status

        logging.info(f"Successfully updated state: {data_state}")
        response = make_response(jsonify({"status": "success", "data": data_state}), 200)
        response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
        response.headers["Pragma"] = "no-cache"
        return response

    except Exception as e:
        logging.error(f"Error during state update: {str(e)}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route("/data", methods=["GET"])
def get_data():
    """Retrieve the current data state."""
    response = make_response(jsonify(data_state), 200)
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    return response

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
