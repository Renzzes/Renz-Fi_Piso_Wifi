export type RouterConfig = {
  host: string;
  username: string;
  password: string;
  profile: string;
  ssid: string;
  wifiPassword?: string;
};

export type RouterPublicConfig = {
  host: string;
  username: string;
  profile: string;
  ssid: string;
  wifiPassword?: string;
  passwordConfigured: boolean;
};

export type RouterTestStep = {
  id: "api_reachable" | "login" | "profile";
  label: string;
  ok: boolean;
  message: string;
};

export type RouterTestResult = {
  ok: boolean;
  steps: RouterTestStep[];
  summary: string;
};
