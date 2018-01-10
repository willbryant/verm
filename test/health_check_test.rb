require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))

class HealthCheckTest < Verm::TestCase
  def setup
    spawn_verm(
      :health_check_path => health_check_path,
      :healthy_if_file => healthy_file,
      :healthy_unless_file => unhealthy_file)
  end

  def health_check_path
    "/_healthy"
  end

  def healthy_file
    File.join(File.dirname(__FILE__), 'tmp', 'healthy')
  end

  def unhealthy_file
    File.join(File.dirname(__FILE__), 'tmp', 'unhealthy')
  end

  def test_successful_normally
    FileUtils.touch(healthy_file)
    FileUtils.rm_f(unhealthy_file)
    get :path => health_check_path, :expected_response_code => 200, :expected_content => "Online\n"
  end

  def test_unsuccessful_if_healthy_file_missing
    FileUtils.rm_f(healthy_file)
    FileUtils.rm_f(unhealthy_file)
    get :path => health_check_path, :expected_response_code => 503, :expected_content => "Offline\n"
  end

  def test_unsuccessful_if_unhealthy_file_present
    FileUtils.touch(healthy_file)
    FileUtils.touch(unhealthy_file)
    get :path => health_check_path, :expected_response_code => 503, :expected_content => "Offline\n"
  end
end
