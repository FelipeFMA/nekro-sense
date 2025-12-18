#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")] // hide console window on Windows in release

use eframe::egui;
use poll_promise::Promise;
use std::process::Command;
use std::collections::VecDeque;

fn hex_to_color(hex: &str) -> egui::Color32 {
    let hex = hex.trim_start_matches('#');
    if hex.len() != 6 {
        return egui::Color32::WHITE;
    }
    let r = u8::from_str_radix(&hex[0..2], 16).unwrap_or(255);
    let g = u8::from_str_radix(&hex[2..4], 16).unwrap_or(255);
    let b = u8::from_str_radix(&hex[4..6], 16).unwrap_or(255);
    egui::Color32::from_rgb(r, g, b)
}

fn color_to_hex(color: egui::Color32) -> String {
    format!("{:02x}{:02x}{:02x}", color.r(), color.g(), color.b())
}

fn main() -> eframe::Result {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([720.0, 600.0])
            .with_min_inner_size([400.0, 300.0]),
        ..Default::default()
    };
    eframe::run_native(
        "Nekro Sense",
        options,
        Box::new(|cc| {
            // This gives us image support:
            egui_extras::install_image_loaders(&cc.egui_ctx);
            Ok(Box::new(NekroApp::new(cc)))
        }),
    )
}

#[derive(PartialEq)]
enum Page {
    Keyboard,
    Power,
    Fans,
}

struct NekroApp {
    current_page: Page,
    status_msg: String,
    is_error: bool,

    // Keyboard State
    kb_per_zone: bool,
    kb_single_color: bool,
    kb_colors: [String; 4],
    kb_brightness: i32,
    
    kb_effect_mode: String,
    kb_effect_speed: i32,
    kb_effect_brightness: i32,
    kb_effect_direction: i32,
    kb_effect_color: String,

    logo_on: bool,
    logo_color: String,
    logo_brightness: i32,

    // Power State
    power_choices: Vec<String>,
    power_current: String,
    battery_limit: bool,

    // Fans State
    fans_link: bool,
    fans_cpu_auto: bool,
    fans_cpu_val: i32,
    fans_gpu_auto: bool,
    fans_gpu_val: i32,
    fans_current: String,

    show_about: bool,

    // Async command handling
    command_queue: VecDeque<Vec<String>>,
    pending_command: Option<PendingCommand>,
}

struct PendingCommand {
    args: Vec<String>,
    promise: Promise<(bool, String)>,
}

impl NekroApp {
    fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        let mut app = Self {
            current_page: Page::Keyboard,
            status_msg: "Ready".to_string(),
            is_error: false,

            kb_per_zone: true,
            kb_single_color: true,
            kb_colors: [
                "00aaff".to_string(),
                "00aaff".to_string(),
                "00aaff".to_string(),
                "00aaff".to_string(),
            ],
            kb_brightness: 100,

            kb_effect_mode: "wave".to_string(),
            kb_effect_speed: 1,
            kb_effect_brightness: 100,
            kb_effect_direction: 2,
            kb_effect_color: "".to_string(),

            logo_on: true,
            logo_color: "00ffcc".to_string(),
            logo_brightness: 100,

            power_choices: vec!["balanced".to_string(), "performance".to_string(), "power-saver".to_string()],
            power_current: "unknown".to_string(),
            battery_limit: false,

            fans_link: false,
            fans_cpu_auto: true,
            fans_cpu_val: 50,
            fans_gpu_auto: true,
            fans_gpu_val: 50,
            fans_current: "unknown".to_string(),

            show_about: false,

            command_queue: VecDeque::new(),
            pending_command: None,
        };
        app.refresh_all();
        app
    }

    fn run_cmd(&mut self, args: Vec<String>) {
        self.command_queue.push_back(args);
    }

    fn refresh_all(&mut self) {
        self.run_cmd(vec!["power".to_string(), "list".to_string()]);
        self.run_cmd(vec!["power".to_string(), "get".to_string()]);
        self.run_cmd(vec!["battery".to_string(), "get".to_string()]);
        self.run_cmd(vec!["logo".to_string(), "get".to_string()]);
        self.run_cmd(vec!["fan".to_string(), "get".to_string()]);
        self.run_cmd(vec!["rgb".to_string(), "per-zone-get".to_string()]);
        self.run_cmd(vec!["rgb".to_string(), "effect-get".to_string()]);
    }

    fn handle_command_success(&mut self, args: &[String], output: &str) {
        if args.is_empty() { return; }
        
        match args[0].as_str() {
            "power" => {
                if args.len() >= 2 {
                    match args[1].as_str() {
                        "get" => self.power_current = output.to_string(),
                        "list" => self.power_choices = output.split_whitespace().map(|s| s.to_string()).collect(),
                        "set" => {
                            self.status_info(format!("Power profile set to {}", output));
                            // Refresh current profile after setting
                            self.run_cmd(vec!["power".to_string(), "get".to_string()]);
                        }
                        _ => {}
                    }
                }
            }
            "battery" => {
                if args.len() >= 2 {
                    match args[1].as_str() {
                        "get" => self.battery_limit = output.trim() == "1",
                        "on" | "off" | "set" => {
                            self.status_info(output);
                            self.run_cmd(vec!["battery".to_string(), "get".to_string()]);
                        }
                        _ => {}
                    }
                }
            }
            "logo" => {
                if args.len() >= 2 && args[1] == "get" {
                    // Format: RRGGBB,brightness,enable
                    let parts: Vec<&str> = output.split(',').collect();
                    if parts.len() >= 3 {
                        self.logo_color = parts[0].to_string();
                        self.logo_brightness = parts[1].parse().unwrap_or(100);
                        self.logo_on = parts[2].trim() == "1";
                    }
                } else {
                    self.status_info(output);
                }
            }
            "fan" => {
                if args.len() >= 2 && args[1] == "get" {
                    self.fans_current = output.to_string();
                    // Parse "cpu,gpu"
                    let parts: Vec<&str> = output.split(',').collect();
                    if parts.len() >= 2 {
                        let cpu: i32 = parts[0].trim().parse().unwrap_or(0);
                        let gpu: i32 = parts[1].trim().parse().unwrap_or(0);
                        self.fans_cpu_auto = cpu == 0;
                        self.fans_gpu_auto = gpu == 0;
                        if cpu > 0 { self.fans_cpu_val = cpu; }
                        if gpu > 0 { self.fans_gpu_val = gpu; }
                    }
                } else {
                    self.status_info(output);
                    self.run_cmd(vec!["fan".to_string(), "get".to_string()]);
                }
            }
            "rgb" => {
                if args.len() >= 2 {
                    match args[1].as_str() {
                        "per-zone-get" => {
                            // Format: c1,c2,c3,c4,brightness
                            let parts: Vec<&str> = output.split(',').collect();
                            if parts.len() >= 5 {
                                for i in 0..4 {
                                    self.kb_colors[i] = parts[i].to_string();
                                }
                                self.kb_brightness = parts[4].trim().parse().unwrap_or(100);
                                // Check if all colors are the same to set kb_single_color
                                self.kb_single_color = self.kb_colors.iter().all(|c| c == &self.kb_colors[0]);
                            }
                        }
                        "effect-get" => {
                            // Format: mode,speed,brightness,dir,r,g,b
                            let parts: Vec<&str> = output.split(',').collect();
                            if parts.len() >= 7 {
                                let mode_id: i32 = parts[0].trim().parse().unwrap_or(0);
                                // Map mode_id back to name
                                let mode_names = ["static", "breathing", "neon", "wave", "shifting", "zoom", "meteor", "twinkling"];
                                if mode_id >= 0 && (mode_id as usize) < mode_names.len() {
                                    self.kb_effect_mode = mode_names[mode_id as usize].to_string();
                                }
                                self.kb_effect_speed = parts[1].trim().parse().unwrap_or(1);
                                self.kb_effect_brightness = parts[2].trim().parse().unwrap_or(100);
                                self.kb_effect_direction = parts[3].trim().parse().unwrap_or(2);
                                let r: u8 = parts[4].trim().parse().unwrap_or(0);
                                let g: u8 = parts[5].trim().parse().unwrap_or(0);
                                let b: u8 = parts[6].trim().parse().unwrap_or(0);
                                if r == 0 && g == 0 && b == 0 {
                                    self.kb_effect_color = "".to_string();
                                } else {
                                    self.kb_effect_color = format!("{:02x}{:02x}{:02x}", r, g, b);
                                }
                            }
                        }
                        "per-zone" | "effect" => {
                            self.status_info(output);
                        }
                        _ => {
                            self.status_info(output);
                        }
                    }
                } else {
                    self.status_info(output);
                }
            }
            _ => {
                self.status_info(output);
            }
        }
    }

    fn status_info(&mut self, msg: impl Into<String>) {
        self.status_msg = msg.into();
        self.is_error = false;
    }

    fn status_error(&mut self, msg: impl Into<String>) {
        self.status_msg = msg.into();
        self.is_error = true;
    }
}

impl eframe::App for NekroApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        // Handle pending command
        if let Some(pending) = self.pending_command.take() {
            if let Some((ok, msg)) = pending.promise.ready() {
                if *ok {
                    self.handle_command_success(&pending.args, msg);
                } else {
                    self.status_error(msg);
                }
            } else {
                // Still pending
                self.pending_command = Some(pending);
            }
        }

        // Start next command if idle
        if self.pending_command.is_none() {
            if let Some(args) = self.command_queue.pop_front() {
                let args_clone = args.clone();
                self.pending_command = Some(PendingCommand {
                    args,
                    promise: Promise::spawn_thread("cmd", move || {
                        run_privileged(args_clone)
                    }),
                });
                // Request a repaint to process the next command soon
                ctx.request_repaint();
            }
        }

        egui::TopBottomPanel::top("header").show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.selectable_value(&mut self.current_page, Page::Keyboard, "Keyboard");
                ui.selectable_value(&mut self.current_page, Page::Power, "Power");
                ui.selectable_value(&mut self.current_page, Page::Fans, "Fans");
                
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("About").clicked() {
                        self.show_about = true;
                    }
                    if ui.button("Refresh").clicked() {
                        self.refresh_all();
                    }
                });
            });
        });

        if self.show_about {
            egui::Window::new("About Nekro Sense")
                .open(&mut self.show_about)
                .show(ctx, |ui| {
                    ui.heading("Nekro Sense");
                    ui.label("Rust + egui GUI for Nekro-Sense.");
                    ui.label("Controls RGB, power profile, and fans via CLI helper.");
                    ui.hyperlink("https://github.com/FelipeFMA/nekro-sense");
                });
        }

        egui::TopBottomPanel::bottom("footer").show(ctx, |ui| {
            let color = if self.is_error {
                egui::Color32::RED
            } else {
                ui.visuals().weak_text_color()
            };
            ui.label(egui::RichText::new(&self.status_msg).color(color));
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                match self.current_page {
                    Page::Keyboard => self.ui_keyboard(ui),
                    Page::Power => self.ui_power(ui),
                    Page::Fans => self.ui_fans(ui),
                }
            });
        });
    }
}

impl NekroApp {
    fn ui_keyboard(&mut self, ui: &mut egui::Ui) {
        ui.heading("Keyboard RGB");
        
        ui.group(|ui| {
            ui.radio_value(&mut self.kb_per_zone, true, "Per-zone static");
            if self.kb_per_zone {
                ui.indent("per_zone", |ui| {
                    ui.checkbox(&mut self.kb_single_color, "Single color for all zones");
                    if self.kb_single_color {
                        ui.horizontal(|ui| {
                            ui.label("Color:");
                            let mut color = hex_to_color(&self.kb_colors[0]);
                            if ui.color_edit_button_srgba(&mut color).changed() {
                                self.kb_colors[0] = color_to_hex(color);
                            }
                            ui.text_edit_singleline(&mut self.kb_colors[0]);
                        });
                    } else {
                        for i in 0..4 {
                            ui.horizontal(|ui| {
                                ui.label(format!("Zone {}:", i + 1));
                                let mut color = hex_to_color(&self.kb_colors[i]);
                                if ui.color_edit_button_srgba(&mut color).changed() {
                                    self.kb_colors[i] = color_to_hex(color);
                                }
                                ui.text_edit_singleline(&mut self.kb_colors[i]);
                            });
                        }
                    }
                    ui.add(egui::Slider::new(&mut self.kb_brightness, 0..=100).text("Brightness"));
                    if ui.button("Apply").clicked() {
                        let mut args = vec!["rgb".to_string(), "per-zone".to_string()];
                        if self.kb_single_color {
                            args.push(self.kb_colors[0].clone());
                        } else {
                            for i in 0..4 {
                                args.push(self.kb_colors[i].clone());
                            }
                        }
                        args.push("-b".to_string());
                        args.push(self.kb_brightness.to_string());
                        self.run_cmd(args);
                        self.run_cmd(vec!["rgb".to_string(), "per-zone-get".to_string()]);
                    }
                });
            }

            ui.radio_value(&mut self.kb_per_zone, false, "Effect");
            if !self.kb_per_zone {
                ui.indent("effect", |ui| {
                    egui::ComboBox::from_label("Mode")
                        .selected_text(&self.kb_effect_mode)
                        .show_ui(ui, |ui| {
                            for mode in ["static", "breathing", "neon", "wave", "shifting", "zoom", "meteor", "twinkling"] {
                                ui.selectable_value(&mut self.kb_effect_mode, mode.to_string(), mode);
                            }
                        });
                    ui.add(egui::Slider::new(&mut self.kb_effect_speed, 0..=9).text("Speed"));
                    ui.add(egui::Slider::new(&mut self.kb_effect_brightness, 0..=100).text("Brightness"));
                    ui.add(egui::Slider::new(&mut self.kb_effect_direction, 1..=2).text("Direction"));
                    ui.horizontal(|ui| {
                        ui.label("Color (optional):");
                        let mut color = if self.kb_effect_color.is_empty() { egui::Color32::BLACK } else { hex_to_color(&self.kb_effect_color) };
                        if ui.color_edit_button_srgba(&mut color).changed() {
                            self.kb_effect_color = color_to_hex(color);
                        }
                        ui.text_edit_singleline(&mut self.kb_effect_color);
                    });
                    if ui.button("Apply").clicked() {
                        let mut args = vec![
                            "rgb".to_string(),
                            "effect".to_string(),
                            self.kb_effect_mode.clone(),
                            "-s".to_string(), self.kb_effect_speed.to_string(),
                            "-b".to_string(), self.kb_effect_brightness.to_string(),
                            "-d".to_string(), self.kb_effect_direction.to_string(),
                        ];
                        if !self.kb_effect_color.is_empty() {
                            args.push("-c".to_string());
                            args.push(self.kb_effect_color.clone());
                        }
                        self.run_cmd(args);
                        self.run_cmd(vec!["rgb".to_string(), "effect-get".to_string()]);
                    }
                });
            }

            ui.separator();
            if ui.button("Turn Off Keyboard Backlight").clicked() {
                self.run_cmd(vec![
                    "rgb".to_string(),
                    "per-zone".to_string(),
                    "ffffff".to_string(),
                    "ffffff".to_string(),
                    "ffffff".to_string(),
                    "ffffff".to_string(),
                    "-b".to_string(),
                    "0".to_string(),
                ]);
                self.run_cmd(vec!["rgb".to_string(), "per-zone-get".to_string()]);
            }
        });

        ui.separator();
        ui.heading("Back Logo");
        ui.group(|ui| {
            ui.checkbox(&mut self.logo_on, "Power");
            ui.horizontal(|ui| {
                ui.label("Color:");
                let mut color = hex_to_color(&self.logo_color);
                if ui.color_edit_button_srgba(&mut color).changed() {
                    self.logo_color = color_to_hex(color);
                }
                ui.text_edit_singleline(&mut self.logo_color);
            });
            ui.add(egui::Slider::new(&mut self.logo_brightness, 0..=100).text("Brightness"));
            if ui.button("Apply Logo").clicked() {
                let mut args = vec![
                    "logo".to_string(),
                    "set".to_string(),
                    self.logo_color.clone(),
                    "-b".to_string(),
                    self.logo_brightness.to_string(),
                ];
                if self.logo_on {
                    args.push("--on".to_string());
                } else {
                    args.push("--off".to_string());
                }
                self.run_cmd(args);
                self.run_cmd(vec!["logo".to_string(), "get".to_string()]);
            }
        });
    }

    fn ui_power(&mut self, ui: &mut egui::Ui) {
        ui.heading("Platform Profile");
        ui.group(|ui| {
            ui.horizontal(|ui| {
                ui.label("Current profile:");
                ui.colored_label(egui::Color32::LIGHT_BLUE, &self.power_current);
            });
            
            let mut selected_choice = None;
            egui::ComboBox::from_label("Select Profile")
                .selected_text(&self.power_current)
                .show_ui(ui, |ui| {
                    for choice in &self.power_choices {
                        if ui.selectable_label(choice == &self.power_current, choice).clicked() {
                            selected_choice = Some(choice.clone());
                        }
                    }
                });
            
            if let Some(choice) = selected_choice {
                self.run_cmd(vec!["power".to_string(), "set".to_string(), choice]);
            }
            
            ui.separator();
            let mut limit = self.battery_limit;
            if ui.checkbox(&mut limit, "Battery limit (80%)").changed() {
                let cmd = if limit { "on" } else { "off" };
                self.run_cmd(vec!["battery".to_string(), cmd.to_string()]);
            }
        });
    }

    fn ui_fans(&mut self, ui: &mut egui::Ui) {
        ui.heading("Fan Control");
        ui.group(|ui| {
            ui.checkbox(&mut self.fans_link, "Link CPU and GPU values");
            
            ui.horizontal(|ui| {
                ui.vertical(|ui| {
                    ui.label("CPU");
                    ui.checkbox(&mut self.fans_cpu_auto, "Auto");
                    if !self.fans_cpu_auto {
                        ui.add(egui::Slider::new(&mut self.fans_cpu_val, 1..=100).text("%"));
                    }
                });
                
                if !self.fans_link {
                    ui.vertical(|ui| {
                        ui.label("GPU");
                        ui.checkbox(&mut self.fans_gpu_auto, "Auto");
                        if !self.fans_gpu_auto {
                            ui.add(egui::Slider::new(&mut self.fans_gpu_val, 1..=100).text("%"));
                        }
                    });
                }
            });

            if ui.button("Apply Fan Settings").clicked() {
                let mut args = vec!["fan".to_string()];
                if self.fans_cpu_auto && (self.fans_link || self.fans_gpu_auto) {
                    args.push("auto".to_string());
                } else {
                    args.push("set".to_string());
                    args.push("--cpu".to_string());
                    args.push(if self.fans_cpu_auto { "auto".to_string() } else { self.fans_cpu_val.to_string() });
                    args.push("--gpu".to_string());
                    let gpu_val = if self.fans_link {
                        if self.fans_cpu_auto { "auto".to_string() } else { self.fans_cpu_val.to_string() }
                    } else {
                        if self.fans_gpu_auto { "auto".to_string() } else { self.fans_gpu_val.to_string() }
                    };
                    args.push(gpu_val);
                }
                self.run_cmd(args);
            }
            
            ui.separator();
            ui.label(format!("Current values (CPU, GPU): {}", self.fans_current));
        });
    }
}

fn run_privileged(args: Vec<String>) -> (bool, String) {
    let python_path = "python3";
    
    // Try to find nekroctl.py in the parent directory or current directory
    let script_path = if std::path::Path::new("../nekroctl.py").exists() {
        "../nekroctl.py"
    } else if std::path::Path::new("nekroctl.py").exists() {
        "nekroctl.py"
    } else {
        // Fallback to absolute path if we can guess it from the workspace info
        "/home/felipe/Documents/Github/nekro-sense/tools/nekroctl.py"
    };

    let mut full_args = vec![script_path.to_string()];
    full_args.extend(args);

    // 1. Try normal
    let output = Command::new(python_path)
        .args(&full_args)
        .output();

    match output {
        Ok(out) if out.status.success() => {
            return (true, String::from_utf8_lossy(&out.stdout).trim().to_string());
        }
        Ok(out) => {
            let err = String::from_utf8_lossy(&out.stderr).to_lowercase();
            let perm_denied = out.status.code() == Some(3) 
                || err.contains("permission denied")
                || err.contains("operation not permitted")
                || err.contains("not authorized")
                || err.contains("authentication is required")
                || err.contains("must be root");

            if !perm_denied {
                return (false, String::from_utf8_lossy(&out.stderr).trim().to_string());
            }
        }
        Err(e) => return (false, e.to_string()),
    }

    // 2. Try sudo -n
    let mut sudo_args = vec!["-n".to_string(), python_path.to_string()];
    sudo_args.extend(full_args.clone());
    let output = Command::new("sudo")
        .args(&sudo_args)
        .output();

    match output {
        Ok(out) if out.status.success() => {
            return (true, String::from_utf8_lossy(&out.stdout).trim().to_string());
        }
        Ok(out) => {
            let err = String::from_utf8_lossy(&out.stderr).to_lowercase();
            let sudo_requires_password = out.status.code() == Some(127)
                || err.contains("a password is required")
                || (err.contains("password") && (err.contains("authentication") || err.contains("is required")))
                || err.contains("no tty present")
                || err.contains("unable to authenticate");

            if !sudo_requires_password {
                return (false, String::from_utf8_lossy(&out.stderr).trim().to_string());
            }
        }
        Err(_) => {}
    }

    // 3. Try pkexec
    let mut pk_args = vec![python_path.to_string()];
    pk_args.extend(full_args);
    let output = Command::new("pkexec")
        .args(&pk_args)
        .output();

    match output {
        Ok(out) if out.status.success() => {
            (true, String::from_utf8_lossy(&out.stdout).trim().to_string())
        }
        Ok(out) => {
            (false, String::from_utf8_lossy(&out.stderr).trim().to_string())
        }
        Err(e) => (false, e.to_string()),
    }
}
