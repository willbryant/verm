require 'fileutils'
require 'net/http'

class VermSpawner
  STARTUP_TIMEOUT = 10 # seconds
  
  attr_reader :verm_binary, :verm_data
  
  def initialize(verm_binary, verm_data)
    @verm_binary = verm_binary
    @verm_data   = verm_data
    raise "Can't see a verm binary at #{verm_binary}" unless File.executable?(verm_binary)
  end
  
  def hostname
    "localhost"
  end
  
  def port
    # real verm will run on 1138, it's convenient to use another port for test so you can test a new version while the old version is still running
    1139
  end
  
  def server_address
    @server_address ||= "http://#{hostname}:#{port}"
  end
  
  def server_uri
    @server_uri ||= URI.parse(server_address)
  end
  
  def clear_data
    FileUtils.rm_r(@verm_data) if File.directory?(@verm_data)
    Dir.mkdir(@verm_data)
  end
  
  def start_verm
    @verm_child_pid = fork { exec @verm_binary, "-d", verm_data, "-l", port.to_s, "-q" }
  end
  
  def server_available?
    !!Net::HTTP.get(server_uri)
  rescue Errno::ECONNREFUSED, Errno::ECONNRESET
    false
  end
  
  def wait_until_available
    1.upto(STARTUP_TIMEOUT*10) do
      return if server_available?
      sleep(0.1)
    end
    raise "Can't connect to our verm instance on #{server_address}"
  end
  
  def wait_until_not_available
    1.upto(STARTUP_TIMEOUT*10) do
      return unless server_available?
      sleep(0.1)
    end
    raise "Gave up waiting for verm instance on #{server_address} to go away"
  end
  
  def stop_verm
    Process.kill('TERM', @verm_child_pid) if @verm_child_pid
    @verm_child_pid = nil
  end
end
