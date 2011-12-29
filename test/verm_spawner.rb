require 'fileutils'
require 'net/http'

class VermSpawner
  STARTUP_TIMEOUT = 10 # seconds
  
  attr_reader :verm_binary, :verm_data, :mime_types_file
  
  def initialize(verm_binary, verm_data, mime_types_file, port = nil, replicate_to = nil)
    @verm_binary = verm_binary
    @verm_data = verm_data
    @mime_types_file = mime_types_file
    @port = port
    @replicate_to = replicate_to
    raise "Can't see a verm binary at #{verm_binary}" unless File.executable?(verm_binary)
  end
  
  def hostname
    "localhost"
  end
  
  def port
    # real verm will run on 1138, it's convenient to use another port for test so you can test a new version while the old version is still running
    @port || 1139
  end

  def host # following javascript Location naming conventions
    "#{hostname}:#{port}"
  end
  
  def server_uri
    @server_uri ||= URI.parse("http://#{host}")
  end
  
  def clear_data
    FileUtils.rm_r(@verm_data) if File.directory?(@verm_data)
    Dir.mkdir(@verm_data)
  end
  
  def start_verm
    exec_args = [@verm_binary, "-d", verm_data, "-l", port.to_s, "-m", mime_types_file]
    exec_args << "-q" unless ENV['NOISY']

    if @replicate_to
      Array(@replicate_to).each {|r| exec_args << '-r'; exec_args << r}
    end
    
    if ENV['VALGRIND']
      exec_args.unshift "--leak-check=full" if ENV['VALGRIND'] == "full"
      exec_args.unshift "valgrind"
    end
    
    @verm_child_pid = fork { exec *exec_args }
  end
  
  def server_available?
    !!Net::HTTP.get(server_uri)
  rescue Errno::ECONNRESET, Errno::EPIPE, EOFError => e
    puts e if ENV['DEBUG']
    retry
  rescue Errno::ECONNREFUSED
    false
  end
  
  def wait_until_available
    1.upto(STARTUP_TIMEOUT*10) do
      return if server_available?
      sleep(0.1)
    end
    raise "Can't connect to our verm instance on #{server_address}"
  end
  
  def stop_verm
    return unless @verm_child_pid
    Process.kill('TERM', @verm_child_pid)
    Process.wait(@verm_child_pid)
    @verm_child_pid = nil
  end
end
