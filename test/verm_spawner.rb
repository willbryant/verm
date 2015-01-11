require 'fileutils'
require 'net/http'

class VermSpawner
  STARTUP_TIMEOUT = 10 # seconds
  
  attr_reader :verm_binary, :verm_data, :options, :capture_stdout_in, :capture_stderr_in
  
  def initialize(verm_binary, verm_data, options = {})
    @verm_binary = verm_binary
    @verm_data = verm_data
    @port = options.delete(:port)
    @replicate_to = options.delete(:replicate_to)
    @capture_stdout_in = options.delete(:capture_stdout_in)
    @capture_stderr_in = options.delete(:capture_stderr_in)
    @options = options
    raise "Can't see a verm binary at #{verm_binary}" unless File.executable?(verm_binary)
  end
  
  def hostname
    "localhost"
  end
  
  def port
    # real verm will run on 3404, it's convenient to use another port for test so you can test a new version while the old version is still running
    @port || 3405
  end

  def host # following javascript Location naming conventions
    "#{hostname}:#{port}"
  end
  
  def server_uri
    @server_uri ||= URI.parse("http://#{host}")
  end

  def setup(extra_options = {})
    clear_data
    start_verm(extra_options)
    wait_until_available
  end

  def teardown
    stop_verm
    clear_data
  end
  
  def clear_data
    FileUtils.rm_r(@verm_data) if File.directory?(@verm_data)
    Dir.mkdir(@verm_data)
  end
  
  def start_verm(extra_options = {})
    exec_args  = [@verm_binary, "--data", verm_data, "--port", port.to_s]
    exec_args << "--quiet" unless ENV['NOISY']

    @options.merge(extra_options).each do |name, value|
      option = "--#{name.to_s.gsub("_", "-")}"
      exec_args += [option, value]
    end

    if @replicate_to
      Array(@replicate_to).each {|r| exec_args << '--replicate-to'; exec_args << r}
    end
    
    if ENV['VALGRIND']
      exec_args.unshift "--leak-check=full" if ENV['VALGRIND'] == "full"
      exec_args.unshift "valgrind"
    end
    
    @verm_child_pid = fork do
      begin
        STDOUT.reopen(@capture_stdout_in, "wb") if @capture_stdout_in
        STDERR.reopen(@capture_stderr_in, "wb") if @capture_stderr_in
      rescue => e
        puts e
        exit 1
      end
      exec *exec_args
    end
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
    raise "Can't connect to our verm instance on #{host}"
  end
  
  def stop_verm
    return unless @verm_child_pid
    Process.kill('TERM', @verm_child_pid)
    Process.wait(@verm_child_pid)
    @verm_child_pid = nil
  end
end
