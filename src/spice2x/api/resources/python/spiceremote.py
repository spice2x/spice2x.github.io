#!/usr/bin/env python3

from datetime import datetime
import tkinter as tk
import tkinter.ttk as ttk
import tkinter.messagebox
try:
    import spiceapi
except ModuleNotFoundError:
    raise RuntimeError("spiceapi module not installed")


NSEW = tk.N+tk.S+tk.E+tk.W


def api_action(func):
    def wrapper(*args, **kwargs):
        try:
            func(*args, **kwargs)
        except spiceapi.APIError as e:
            tk.messagebox.showerror(title="API Error", message=str(e))
        except ValueError as e:
            tk.messagebox.showerror(title="Input Error", message=str(e))
        except (ConnectionResetError, BrokenPipeError, RuntimeError) as e:
            tk.messagebox.showerror(title="Connection Error", message=str(e))
        except AttributeError:
            tk.messagebox.showerror(title="Error", message="No active connection.")
        except BaseException as e:
            tk.messagebox.showerror(title="Exception", message=str(e))
    return wrapper


class TextField(ttk.Frame):
    """Simple text field with a scroll bar to the right."""

    def __init__(self, parent, read_only=False, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.parent = parent
        self.read_only = read_only

        # create scroll bar
        self.scroll = ttk.Scrollbar(self)
        self.scroll.pack(side=tk.RIGHT, fill=tk.Y)

        # create text field
        self.contents = tk.Text(self, height=1, width=50,
                                foreground="black", background="#E8E6E0",
                                insertbackground="black")
        self.contents.pack(side=tk.LEFT, expand=True, fill=tk.BOTH)

        # link scroll bar with text field
        self.scroll.config(command=self.contents.yview)
        self.contents.config(yscrollcommand=self.scroll.set)

        # read only setting
        if self.read_only:
            self.contents.config(state=tk.DISABLED)

    def get_text(self):
        """Get the current text content as string.

        :return: text content
        """
        return self.contents.get("1.0", tk.END+"-1c")

    def set_text(self, text):
        """Set the current text content.

        :param text: text content
        :return: None
        """

        # enable if read only
        if self.read_only:
            self.contents.config(state=tk.NORMAL)

        # delete old content and insert replacement text
        self.contents.delete("1.0", tk.END)
        self.contents.insert(tk.END, text)

        # disable if read only
        if self.read_only:
            self.contents.config(state=tk.DISABLED)


class ManualTab(ttk.Frame):
    """Manual JSON request/response functinality."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent
        self.rowconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)
        self.columnconfigure(0, weight=1)

        # request text field
        self.txt_request = TextField(self, read_only=False)
        self.txt_request.grid(row=0, column=0, sticky=NSEW, padx=2, pady=2)
        self.txt_request.set_text(
            '{\n'
            '  "id": 1,\n'
            '  "module": "coin",\n'
            '  "function": "insert",\n'
            '  "params": []\n'
            '}\n'
        )

        # response text field
        self.txt_response = TextField(self, read_only=False)
        self.txt_response.grid(row=1, column=0, sticky=NSEW, padx=2, pady=2)
        #self.txt_response.contents.config(state=tk.DISABLED)

        # send button
        self.btn_send = ttk.Button(self, text="Send", command=self.action_send)
        self.btn_send.grid(row=2, column=0, sticky=tk.W+tk.E, padx=2, pady=2)

    def action_send(self):
        """Gets called when the send button is pressed."""

        # check connection
        if self.app.connection:

            # send request and get response
            self.txt_response.set_text("Sending...")
            try:

                # build request
                request = spiceapi.Request.from_json(self.txt_request.get_text())

                # send request and get response, measure time
                t1 = datetime.now()
                response = self.app.connection.request(request)
                t2 = datetime.now()

                # set response text
                self.txt_response.set_text("{}\n\nElapsed time: {} seconds".format(
                    response.to_json(),
                    (t2 - t1).total_seconds())
                )

            except spiceapi.APIError as e:
                self.txt_response.set_text(f"Server returned error:\n{e}")
            except spiceapi.MalformedRequestException:
                self.txt_response.set_text("Malformed request detected.")
            except (ConnectionResetError, BrokenPipeError, RuntimeError) as e:
                self.txt_response.set_text("Error sending request: " + str(e))
            except BaseException as e:
                self.txt_response.set_text("General Exception: " + str(e))

        else:

            # print error
            self.txt_response.set_text("No active connection.")


class ControlTab(ttk.Frame):
    """Main control tab."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent

        # scale grid
        self.columnconfigure(0, weight=1)

        # card
        self.card = ttk.Frame(self, padding=(8, 8, 8, 8))
        self.card.grid(row=0, column=0, sticky=tk.E+tk.W)
        self.card.columnconfigure(0, weight=1)
        self.card.columnconfigure(1, weight=1)
        self.card_lbl = ttk.Label(self.card, text="Card")
        self.card_lbl.grid(row=0, columnspan=2)
        self.card_entry = ttk.Entry(self.card)
        self.card_entry.insert(tk.END, "E004010000000000")
        self.card_entry.grid(row=1, columnspan=2, sticky=NSEW, padx=2, pady=2)
        self.card_insert_p1 = ttk.Button(self.card, text="Insert P1", command=self.action_insert_p1)
        self.card_insert_p1.grid(row=2, column=0, sticky=NSEW, padx=2, pady=2)
        self.card_insert_p2 = ttk.Button(self.card, text="Insert P2", command=self.action_insert_p2)
        self.card_insert_p2.grid(row=2, column=1, sticky=NSEW, padx=2, pady=2)

        # coin
        self.coin = ttk.Frame(self, padding=(8, 8, 8, 8))
        self.coin.grid(row=1, column=0, sticky=tk.E+tk.W)
        self.coin.columnconfigure(0, weight=1)
        self.coin.columnconfigure(1, weight=1)
        self.coin.columnconfigure(2, weight=1)
        self.coin_lbl = ttk.Label(self.coin, text="Coins")
        self.coin_lbl.grid(row=0, columnspan=3)
        self.coin_entry = ttk.Entry(self.coin)
        self.coin_entry.insert(tk.END, "1")
        self.coin_entry.grid(row=1, columnspan=3, sticky=NSEW, padx=2, pady=2)
        self.coin_set = ttk.Button(self.coin, text="Set to Amount", command=self.action_coin_set)
        self.coin_set.grid(row=2, column=0, sticky=NSEW, padx=2, pady=2)
        self.coin_insert = ttk.Button(self.coin, text="Insert Amount", command=self.action_coin_insert)
        self.coin_insert.grid(row=2, column=1, sticky=NSEW, padx=2, pady=2)
        self.coin_insert = ttk.Button(self.coin, text="Insert Single", command=self.action_coin_insert_single)
        self.coin_insert.grid(row=2, column=2, sticky=NSEW, padx=2, pady=2)

    @api_action
    def action_insert(self, unit: int):
        spiceapi.card_insert(self.app.connection, unit, self.card_entry.get())

    @api_action
    def action_insert_p1(self):
        return self.action_insert(0)

    @api_action
    def action_insert_p2(self):
        return self.action_insert(1)

    @api_action
    def action_coin_set(self):
        spiceapi.coin_set(self.app.connection, int(self.coin_entry.get()))

    @api_action
    def action_coin_insert(self):
        spiceapi.coin_insert(self.app.connection, int(self.coin_entry.get()))

    @api_action
    def action_coin_insert_single(self):
        spiceapi.coin_insert(self.app.connection)


class InfoTab(ttk.Frame):
    """The info tab."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        # info text field
        self.txt_info = TextField(self, read_only=True)
        self.txt_info.grid(row=0, column=0, sticky=NSEW, padx=2, pady=2)

        # refresh button
        self.btn_refresh = ttk.Button(self, text="Refresh", command=self.action_refresh)
        self.btn_refresh.grid(row=1, column=0, sticky=tk.W+tk.E, padx=2, pady=2)

    @api_action
    def action_refresh(self):

        # get information
        avs = spiceapi.info_avs(self.app.connection)
        launcher = spiceapi.info_launcher(self.app.connection)
        memory = spiceapi.info_memory(self.app.connection)

        # build text
        avs_text = ""
        for k, v in avs.items():
            avs_text += f"{k}: {v}\n"
        launcher_text = ""
        for k, v in launcher.items():
            if isinstance(v, list):
                launcher_text += f"{k}:\n"
                for i in v:
                    launcher_text += f"  {i}\n"
            else:
                launcher_text += f"{k}: {v}\n"
        memory_text = ""
        for k, v in memory.items():
            memory_text += f"{k}: {v}\n"

        # set text
        self.txt_info.set_text(
            f"AVS:\n{avs_text}\n"
            f"Launcher:\n{launcher_text}\n"
            f"Memory:\n{memory_text}"
        )


class ButtonsTab(ttk.Frame):
    """The buttons tab."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        # button text field
        self.txt_buttons = TextField(self, read_only=True)
        self.txt_buttons.grid(row=0, column=0, sticky=NSEW, padx=2, pady=2)

        # refresh button
        self.btn_refresh = ttk.Button(self, text="Refresh", command=self.action_refresh)
        self.btn_refresh.grid(row=1, column=0, sticky=tk.W+tk.E, padx=2, pady=2)

    @api_action
    def action_refresh(self):

        # get states
        states = spiceapi.buttons_read(self.app.connection)

        # build text
        txt = ""
        for name, velocity, active in states:
            state = "on" if velocity > 0 else "off"
            active_txt = "" if active else " (inactive)"
            txt += f"{name}: {state}{active_txt}\n"
        if len(states) == 0:
            txt = "No buttons available."

        # set text
        self.txt_buttons.set_text(txt)


class AnalogsTab(ttk.Frame):
    """The analogs tab."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        # button text field
        self.txt_analogs = TextField(self, read_only=True)
        self.txt_analogs.grid(row=0, column=0, sticky=NSEW, padx=2, pady=2)

        # refresh button
        self.btn_refresh = ttk.Button(self, text="Refresh", command=self.action_refresh)
        self.btn_refresh.grid(row=1, column=0, sticky=tk.W+tk.E, padx=2, pady=2)

    @api_action
    def action_refresh(self):

        # get states
        states = spiceapi.analogs_read(self.app.connection)

        # build text
        txt = ""
        for name, value, active in states:
            value_txt = round(value, 2)
            active_txt = "" if active else " (inactive)"
            txt += f"{name}: {value_txt}{active_txt}\n"
        if len(states) == 0:
            txt = "No analogs available."

        # set text
        self.txt_analogs.set_text(txt)


class LightsTab(ttk.Frame):
    """The lights tab."""

    def __init__(self, app, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.app = app
        self.parent = parent
        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)

        # light text field
        self.txt_lights = TextField(self, read_only=True)
        self.txt_lights.grid(row=0, column=0, sticky=NSEW, padx=2, pady=2)

        # refresh button
        self.btn_refresh = ttk.Button(self, text="Refresh", command=self.action_refresh)
        self.btn_refresh.grid(row=1, column=0, sticky=tk.W+tk.E, padx=2, pady=2)

    @api_action
    def action_refresh(self):

        # get states
        states = spiceapi.lights_read(self.app.connection)

        # build text
        txt = ""
        for name, value, active in states:
            value_txt = round(value, 2)
            active_txt = "" if active else " (inactive)"
            txt += f"{name}: {value_txt}{active_txt}\n"
        if len(states) == 0:
            txt = "No lights available."

        # set text
        self.txt_lights.set_text(txt)


class MainApp(ttk.Frame):
    """The main application frame."""

    def __init__(self, parent, **kwargs):

        # init frame
        ttk.Frame.__init__(self, parent, **kwargs)
        self.parent = parent
        self.connection = None

        self.tabs = ttk.Notebook(self)
        self.tab_control = ControlTab(self, self.tabs)
        self.tabs.add(self.tab_control, text="Control")
        self.tab_info = InfoTab(self, self.tabs)
        self.tabs.add(self.tab_info, text="Info")
        self.tab_buttons = ButtonsTab(self, self.tabs)
        self.tabs.add(self.tab_buttons, text="Buttons")
        self.tab_analogs = AnalogsTab(self, self.tabs)
        self.tabs.add(self.tab_analogs, text="Analogs")
        self.tab_lights = LightsTab(self, self.tabs)
        self.tabs.add(self.tab_lights, text="Lights")
        self.tab_manual = ManualTab(self, self.tabs)
        self.tabs.add(self.tab_manual, text="Manual")
        self.tabs.pack(expand=True, fill=tk.BOTH)

        # connection panel
        self.frm_connection = ttk.Frame(self)

        # host: [field]
        self.lbl_host = ttk.Label(self.frm_connection, text="Host:")
        self.txt_host = ttk.Entry(self.frm_connection, width=15)
        self.lbl_host.columnconfigure(0, weight=1)
        self.txt_host.columnconfigure(1, weight=1)
        self.txt_host.insert(tk.END, "localhost")

        # port: [field]
        self.lbl_port = ttk.Label(self.frm_connection, text="Port:")
        self.txt_port = ttk.Entry(self.frm_connection, width=5)
        self.lbl_port.columnconfigure(2, weight=1)
        self.txt_port.columnconfigure(3, weight=1)
        self.txt_port.insert(tk.END, "1337")

        # pass: [field]
        self.lbl_pw = ttk.Label(self.frm_connection, text="Pass:")
        self.txt_pw = ttk.Entry(self.frm_connection, width=10)
        self.lbl_pw.columnconfigure(4, weight=1)
        self.txt_pw.columnconfigure(5, weight=1)
        self.txt_pw.insert(tk.END, "debug")

        # grid setup
        self.lbl_host.grid(row=0, column=0, sticky=tk.W+tk.E, padx=2)
        self.txt_host.grid(row=0, column=1, sticky=tk.W+tk.E, padx=2)
        self.lbl_port.grid(row=0, column=2, sticky=tk.W+tk.E, padx=2)
        self.txt_port.grid(row=0, column=3, sticky=tk.W+tk.E, padx=2)
        self.lbl_pw.grid(row=0, column=4, sticky=tk.W+tk.E, padx=2)
        self.txt_pw.grid(row=0, column=5, sticky=tk.W+tk.E, padx=2)
        self.frm_connection.pack(fill=tk.NONE, pady=2)

        # send/connect/disconnect buttons panel
        self.frm_connect = ttk.Frame(self)

        # connect button
        self.btn_connect = ttk.Button(
            self.frm_connect,
            text="Connect",
            command=self.action_connect,
            width=10)

        # disconnect button
        self.btn_disconnect = ttk.Button(
            self.frm_connect,
            text="Disconnect",
            command=self.action_disconnect,
            width=10)

        # kill button
        self.btn_kill = ttk.Button(
            self.frm_connect,
            text="Kill",
            command=self.action_kill,
            width=10)

        # restart button
        self.btn_restart = ttk.Button(
            self.frm_connect,
            text="Restart",
            command=self.action_restart,
            width=10)

        # grid setup
        self.btn_connect.grid(row=0, column=1, sticky=tk.W+tk.E, padx=2)
        self.btn_disconnect.grid(row=0, column=2, sticky=tk.W+tk.E, padx=2)
        self.btn_kill.grid(row=0, column=3, sticky=tk.W+tk.E, padx=2)
        self.btn_restart.grid(row=0, column=4, sticky=tk.W+tk.E, padx=2)
        self.frm_connect.pack(fill=tk.NONE, pady=2)

    def action_connect(self):
        """Gets called when the connect button is pressed."""

        # retrieve connection info
        host = self.txt_host.get()
        port = self.txt_port.get()
        password = self.txt_pw.get()

        # check input
        if not port.isdigit():
            tk.messagebox.showerror("Connection Error", f"Port '{port}' is not a valid number.")
            return

        # create a new connection
        try:
            self.connection = spiceapi.Connection(host=host, port=int(port), password=password)
        except OSError as e:
            tk.messagebox.showerror("Connection Error", "Failed to connect: " + str(e))
            return

        # print success
        tk.messagebox.showinfo("Success", "Connected.")

    def action_disconnect(self):
        """Gets called when the disconnect button is pressed."""

        # check connection
        if self.connection:

            # close connection
            self.connection.close()
            self.connection = None
            tk.messagebox.showinfo("Success", "Closed connection.")

        else:

            # print error
            tk.messagebox.showinfo("Error", "No active connection.")

    def action_kill(self):
        """Gets called when the kill button is pressed."""

        # check connection
        if self.connection:
            spiceapi.control_exit(self.connection)
            self.connection = None
        else:
            tk.messagebox.showinfo("Error", "No active connection.")

    def action_restart(self):
        """Gets called when the restart button is pressed."""

        # check connection
        if self.connection:
            spiceapi.control_restart(self.connection)
            self.connection = None
        else:
            tk.messagebox.showinfo("Error", "No active connection.")


if __name__ == "__main__":

    # create root
    root = tk.Tk()
    root.title("SpiceRemote")
    root.geometry("500x300")

    # set theme
    preferred_theme = "clam"
    s = ttk.Style(root)
    if preferred_theme in s.theme_names():
        s.theme_use(preferred_theme)

    # add application
    MainApp(root).pack(side=tk.TOP, fill=tk.BOTH, expand=True)

    # run root
    root.mainloop()
